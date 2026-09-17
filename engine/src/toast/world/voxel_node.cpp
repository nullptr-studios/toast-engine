#include "voxel_node.hpp"

#include <algorithm>
#include <toast/log.hpp>
#include <toast/physics/contact_events.hpp>
#include <toast/physics/simulator.hpp>
#include <toast/renderer/vulkan_renderer.hpp>
#include <toast/voxel/runtime_pool.hpp>

namespace toast {

namespace {

/// A UID shared by two assets resolves to the wrong one so check through the untyped base
template<typename T>
[[nodiscard]]
auto voxelNodeAssetOfType(const assets::Handle<T>& handle, std::string_view type) -> const T* {
	if (!handle.hasValue()) {
		return nullptr;
	}
	const assets::Asset& asset = static_cast<const assets::HandleBase&>(handle).get();
	return asset.type() == type ? static_cast<const T*>(&asset) : nullptr;
}

}

auto VoxelNode::paletteUid() const -> uint64_t {
	if (m_palette.uid().data() != 0) {
		return m_palette.uid().data();
	}
	if (const auto* model = voxelNodeAssetOfType(m_model, "voxel_model")) {
		return model->paletteUid();
	}
	return 0;
}

void VoxelNode::setModel(assets::Handle<assets::VoxelModel> model) {
	if (m_model == model) {
		return;
	}
	releaseVolume();
	m_model = std::move(model);
	m_model_palette = {};
	m_material_library = {};
	++m_revision;
}

void VoxelNode::setPalette(assets::Handle<assets::VoxelPalette> palette) {
	if (m_palette == palette) {
		return;
	}
	m_palette = std::move(palette);
	m_material_library = {};
	++m_revision;
}

auto VoxelNode::resolvedModel() const -> const assets::VoxelModel* {
	return voxelNodeAssetOfType(m_model, "voxel_model");
}

auto VoxelNode::localBoundingSphere() const -> glm::vec4 {
	const auto* model = voxelNodeAssetOfType(m_model, "voxel_model");
	if (model == nullptr) {
		return glm::vec4(0.0f);
	}
	const glm::vec3 extent = glm::vec3(model->brickDims()) * voxel::k_brick_size;
	return {extent * 0.5f, glm::length(extent) * 0.5f};
}

auto VoxelNode::latticePlacement() const -> std::optional<voxel::LatticePlacement> {
	return voxel::placementFromTransform(getWorldTransform());
}

auto VoxelNode::volume() -> voxel::Volume* {
	const assets::VoxelModel* model = voxelNodeAssetOfType(m_model, "voxel_model");

	if (m_model.hasValue() && model == nullptr && m_reported_wrong_model != m_model.uid().data()) {
		m_reported_wrong_model = m_model.uid().data();
		TOAST_WARN(
		    "Voxel",
		    "'{}' names model {}, which resolves to a '{}' asset, not a voxel model - two assets share that UID. Reimport the "
		    "source to give them separate ones",
		    name(),
		    m_model.uid().get(),
		    static_cast<const assets::HandleBase&>(m_model).get().type()
		);
	}

	if (model != m_instanced_from) {
		releaseVolume();
		m_instanced_from = model;
		m_model_palette = {};

		if (model != nullptr) {
			m_volume = model->instantiate(voxel::runtimeBrickPool());
			if (!m_volume.has_value()) {
				TOAST_WARN(
				    "Voxel",
				    "'{}' could not be instantiated: the runtime brick pool is out of its {} bricks",
				    name(),
				    voxel::k_runtime_brick_capacity
				);
			}
		}
		++m_revision;
	}
	return m_volume.has_value() ? &*m_volume : nullptr;
}

auto VoxelNode::resolvedPalette() -> const voxel::Palette* {
	if (const auto* palette = voxelNodeAssetOfType(m_palette, "voxel_palette")) {
		return &palette->palette();
	}

	if (m_palette.hasValue() && m_reported_wrong_palette != m_palette.uid().data()) {
		m_reported_wrong_palette = m_palette.uid().data();
		TOAST_WARN(
		    "Voxel",
		    "'{}' names palette {}, which resolves to a '{}' asset, not a voxel palette - two assets share that UID",
		    name(),
		    m_palette.uid().get(),
		    static_cast<const assets::HandleBase&>(m_palette).get().type()
		);
	}

	if (m_palette.uid().data() != 0) {
		return nullptr;
	}

	const auto* model = voxelNodeAssetOfType(m_model, "voxel_model");
	if (model == nullptr || model->paletteUid() == 0) {
		return nullptr;
	}
	if (m_model_palette.uid().data() != model->paletteUid()) {
		m_model_palette = assets::load<assets::VoxelPalette>(UID(model->paletteUid()));
	}
	const auto* palette = voxelNodeAssetOfType(m_model_palette, "voxel_palette");
	return palette != nullptr ? &palette->palette() : nullptr;
}

auto VoxelNode::resolvedMaterialLibrary() -> const voxel::MaterialLibrary* {
	const assets::VoxelPalette* palette = voxelNodeAssetOfType(m_palette, "voxel_palette");
	if (palette == nullptr && m_palette.uid().data() == 0) {
		const assets::VoxelModel* model = resolvedModel();
		if (model != nullptr && model->paletteUid() != 0) {
			if (m_model_palette.uid().data() != model->paletteUid()) {
				m_model_palette = assets::load<assets::VoxelPalette>(UID(model->paletteUid()));
			}
			palette = voxelNodeAssetOfType(m_model_palette, "voxel_palette");
		}
	}
	if (palette == nullptr || palette->libraryUid() == 0) {
		return nullptr;
	}

	if (m_material_library.uid().data() != palette->libraryUid()) {
		m_material_library = assets::load<assets::VoxelMaterialLibrary>(UID(palette->libraryUid()));
	}
	const auto* library = voxelNodeAssetOfType(m_material_library, "voxel_material_library");
	return library != nullptr ? &library->library() : nullptr;
}

void VoxelNode::releaseVolume() {
	m_volume.reset();
	m_instanced_from = nullptr;
}

void VoxelNode::sleep() {
	physics::Simulator::sleepBody(bodyID());
}

void VoxelNode::wake() {
	physics::Simulator::wakeBody(bodyID());
}

void VoxelNode::publishPhysicsState(
    bool is_awake, const glm::vec3& current_linear_velocity, const glm::vec3& current_angular_velocity
) {
	const bool state_changed = awake != is_awake;
	awake = is_awake;
	linear_velocity = current_linear_velocity;
	angular_velocity = current_angular_velocity;

	if (not state_changed) {
		return;
	}

	if (awake) {
		woke_up.fire();
	} else {
		went_to_sleep.fire();
	}
}

void VoxelNode::applyPhysicsTransform(const glm::vec3& position, const glm::quat& rotation) {
	world_position = position;
	world_rotation = rotation;
	syncTransform();
}

void VoxelNode::handleContactBegin(const physics::BroadPhasePair& pair) {
	physics::BodyID other_body;
	if (pair.a.body == m_body) {
		other_body = pair.b.body;
	} else if (pair.b.body == m_body) {
		other_body = pair.a.body;
	} else {
		return;
	}

	const auto active = std::ranges::find(m_active_contacts, other_body, &ActiveContact::other_body);
	if (active != m_active_contacts.end()) {
		++active->shape_pair_count;
		return;
	}

	const toast::Box<toast::Node> other_node = physics::Simulator::nodeFor(other_body);
	m_active_contacts.emplace_back(ActiveContact {.other_body = other_body, .other_node = other_node, .shape_pair_count = 1});
	contact_begin.fire(other_node);
}

void VoxelNode::handleContactEnd(const physics::BroadPhasePair& pair) {
	physics::BodyID other_body;
	if (pair.a.body == m_body) {
		other_body = pair.b.body;
	} else if (pair.b.body == m_body) {
		other_body = pair.a.body;
	} else {
		return;
	}

	const auto active = std::ranges::find(m_active_contacts, other_body, &ActiveContact::other_body);
	if (active == m_active_contacts.end()) {
		return;
	}

	if (active->shape_pair_count > 1) {
		--active->shape_pair_count;
		return;
	}

	const toast::Box<toast::Node> other_node = active->other_node;
	m_active_contacts.erase(active);
	contact_end.fire(other_node);
}

void VoxelNode::updateInspectorMessages() {
	static const toast::NodeMessage model_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 1,
	  .text = "Voxel node has no valid model",
	};
	static const toast::NodeMessage palette_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 1,
	  .text = "Voxel node has no valid palette",
	};
	static const toast::NodeMessage material_message {
	  .severity = toast::NodeMessage::warning,
	  .id = 1,
	  .text = "Voxel node has no valid physical material library",
	};
	if (resolvedModel() != nullptr) {
		removeInspectorMessage(model_message);
	} else {
		addInspectorMessage(model_message);
	}
	if (resolvedPalette() != nullptr) {
		removeInspectorMessage(palette_message);
	} else {
		addInspectorMessage(palette_message);
	}
	if (resolvedMaterialLibrary() != nullptr) {
		removeInspectorMessage(material_message);
	} else {
		addInspectorMessage(material_message);
	}
}

void VoxelNode::init() {
	m_registered_proxy = renderer::registerVoxelNodeProxy(this);
}

void VoxelNode::begin() {
	if (not m_registration_requested && participatesIn(NodeOwnerParticipation::gameplay_tick)) {
		m_registration_requested = true;
		physics::Simulator::registerVoxelNode(*this);
		listener().subscribe<event::ContactBegin>("voxel_contact_begin", [this](const event::ContactBegin& contact) {
			handleContactBegin(contact.contact.pair);
		});
		listener().subscribe<event::ContactEnd>("voxel_contact_end", [this](const event::ContactEnd& contact) {
			handleContactEnd(contact.pair);
		});
	}
}

void VoxelNode::end() {
	if (m_registration_requested) {
		m_registration_requested = false;
		listener().unsubscribe<event::ContactBegin>("voxel_contact_begin");
		listener().unsubscribe<event::ContactEnd>("voxel_contact_end");
		m_active_contacts.clear();
		physics::Simulator::unregisterVoxelNode(*this);
	}
	releaseVolume();
	if (!m_registered_proxy) {
		return;
	}

	renderer::unregisterVoxelNodeProxy(this);
	m_registered_proxy = false;
}

void VoxelNode::destroy() {
	end();
}

void VoxelNode::onEnable() {
	physics::Simulator::setBodyEnabled(m_body, true);
	physics::Simulator::setShapeEnabled(m_shape, true);
}

void VoxelNode::onDisable() {
	physics::Simulator::setBodyEnabled(m_body, false);
	physics::Simulator::setShapeEnabled(m_shape, false);
}

}
