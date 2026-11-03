#include "brush_composer.hpp"

#include <atomic>
#include <exception>
#include <mutex>
#include <toast/assets/assets.hpp>
#include <toast/assets/prefab.hpp>
#include <toast/log.hpp>
#include <toast/reflect/reflect.hpp>
#include <toast/voxel/assets/voxel_model.hpp>
#include <toast/voxel/nodes/procedural_voxel.hpp>
#include <toast/world/node_owner.hpp>
#include <unordered_map>
#include <vector>

namespace voxel {

namespace {

class BrushOwner final : public toast::INodeOwner {
public:
	auto name() -> std::string override { return "BrushOwner"; }

	void tick() override { }

	[[nodiscard]]
	auto participatesIn(toast::NodeOwnerParticipation /*use*/) const noexcept -> bool override {
		return false;
	}

	[[nodiscard]]
	auto receivesEvents() const noexcept -> bool override {
		return false;
	}

	void registerDependency(toast::Node& /*from*/, toast::Node& /*to*/) override { }

	void unregisterDependency(toast::Node& /*from*/, toast::Node& /*to*/) override { }

	auto findFrom(const toast::Node& /*origin*/, std::string_view /*query*/) -> toast::Box<toast::Node> override { return {}; }

	auto findFrom(const toast::Node& /*origin*/, const toast::UID& /*uid*/) -> toast::Box<toast::Node> override { return {}; }

	auto searchFrom(const toast::Node& /*origin*/, std::string_view /*query*/) -> std::vector<toast::Box<toast::Node>> override {
		return {};
	}

	auto build(const assets::Handle<assets::Prefab>& file) -> toast::Box<toast::Node> {
		InstantiateContext context;
		context.resolver = [](toast::UID uid) { return assets::load<assets::Prefab>(uid); };
		return instantiate(file, context);
	}

protected:
	void applyActiveCamera() override { }
};

struct Entry {
	uint64_t generation = 0;
	std::unique_ptr<assets::VoxelModel> model;
};

std::recursive_mutex g_mutex;
std::unordered_map<const assets::Prefab*, Entry> g_cache;
std::vector<std::unique_ptr<assets::VoxelModel>> g_retired;
std::atomic<uint64_t> g_generation {1};

void enableTree(toast::Node& node) {
	node.enabled(true);
	for (const toast::Box<toast::Node>& child : node.children()) {
		if (child.exists()) {
			enableTree(const_cast<toast::Node&>(*child));
		}
	}
}

}

auto isBrushPrefab(const assets::Prefab& prefab) -> bool {
	return !prefab.nodes.empty() && prefab.nodes.front().type == "toast::VoxelBrush";
}

auto composeBrushTree(toast::ProceduralVoxel& brush) -> std::unique_ptr<assets::VoxelModel> {
	const std::optional<toast::ProceduralVoxel::Composed> composed = brush.composeAll();
	if (!composed.has_value()) {
		return nullptr;
	}
	// The size comes from the boxes of the pieces
	std::unique_ptr<assets::VoxelModel> model = assets::VoxelModel::capture(composed->volume, brush.paletteUid());
	return model->solidVoxelCount() > 0 ? std::move(model) : nullptr;
}

auto brushModel(const assets::Prefab& prefab, toast::UID uid) -> const assets::VoxelModel* {
	if (!isBrushPrefab(prefab)) {
		return nullptr;
	}

	std::scoped_lock lock(g_mutex);
	Entry& entry = g_cache[&prefab];
	const uint64_t now = g_generation.load();
	if (entry.generation == now) {
		return entry.model.get();
	}
	// a brush that uses itself finds nothing
	entry.generation = now;
	if (entry.model != nullptr) {
		g_retired.push_back(std::move(entry.model));
	}

	try {
		BrushOwner owner;
		const assets::Handle<assets::Prefab> handle(const_cast<assets::Prefab*>(&prefab), uid, "");
		toast::Box<toast::Node> root = owner.build(handle);
		if (!root.exists()) {
			TOAST_ERROR("VoxelBrush", "Could not build the nodes of brush {}", uid.data());
			return nullptr;
		}
		enableTree(*root);
		if (auto* shape = reflect_cast<toast::ProceduralVoxel>(&*root)) {
			entry.model = composeBrushTree(*shape);
		}
	} catch (const std::exception& err) { TOAST_ERROR("VoxelBrush", "Could not compose brush {}: {}", uid.data(), err.what()); }
	return entry.model.get();
}

void invalidateBrushes() noexcept {
	g_generation.fetch_add(1);
}

}
