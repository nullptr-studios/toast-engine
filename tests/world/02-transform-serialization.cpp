#include "test_registry.hpp"
#include "toast/world/world_test_access.hpp"

#include <cassert>
#include <cmath>
#include <toast/assets/prefab.hpp>
#include <toast/world/node_3d.hpp>

using namespace toast;
using WorldTestAccess = toast::_detail::WorldTestAccess;

namespace {

auto close(float lhs, float rhs) -> bool {
	return std::abs(lhs - rhs) < 0.0001f;
}

auto close(const glm::vec3& lhs, const glm::vec3& rhs) -> bool {
	return close(lhs.x, rhs.x) && close(lhs.y, rhs.y) && close(lhs.z, rhs.z);
}

}

TOAST_TEST_NAMED("World", "world/02-transform-serialization", test_world_02_transform_serialization) {
	assets::Prefab source;
	assets::Prefab::BasicNode data {
	  .name = "transform",
	  .type = "toast::Node3D",
	};
	data.fields.push_back({"m_uid", FieldType::uid_t, false, UID(UID::fromString("Transform00"))});
	data.fields.push_back({
	  "position", FieldType::vec3_t, false, glm::vec3 {2.0f, 3.0f, 4.0f}
	});
	data.fields.push_back({
	  "world_position", FieldType::vec3_t, false, glm::vec3 {2.0f, 3.0f, 4.0f}
	});
	source.nodes.push_back(std::move(data));

	auto world = WorldTestAccess::createWorld();
	INodeOwner::InstantiateContext context;
	context.resolver = [](UID) { return assets::Handle<assets::Prefab> {}; };
	assets::Handle<assets::Prefab> handle(&source, UID::fromString("Source00000"), "");

	Box<Node> root = WorldTestAccess::instantiate(*world, handle, context);
	Box<Node3D> transform = root.as<Node3D>();
	assert(transform.exists());
	const FieldInfo* world_position = transform->info()->search("world_position");
	assert(world_position != nullptr);
	assert(world_position->hasAttribute("NoSerialize"));

	transform->syncTransform();
	assert(close(transform->position, {2.0f, 3.0f, 4.0f}));
	assert(close(transform->world_position, {2.0f, 3.0f, 4.0f}));

	assets::Prefab saved(*transform);
	assert(saved.nodes.size() == 1);
	assert(saved.nodes[0].find("position").has_value());
	assert(!saved.nodes[0].find("world_position").has_value());
	assert(!saved.nodes[0].find("world_rotation").has_value());
	assert(!saved.nodes[0].find("world_scale").has_value());
}
