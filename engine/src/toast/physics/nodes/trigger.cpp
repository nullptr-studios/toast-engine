#include "trigger.hpp"

#include "../simulator.hpp"
#include "box_collider.hpp"
#include "capsule_collider.hpp"
#include "sphere_collider.hpp"

namespace physics {

namespace {

[[nodiscard]]
auto isShapeCollider(const toast::Box<toast::Node>& child) -> bool {
	return child.as<SphereCollider>().exists() || child.as<BoxCollider>().exists() || child.as<CapsuleCollider>().exists();
}

}

void Trigger::updateInspectorMessages() {
	static const toast::NodeMessage message {
	  .severity = toast::NodeMessage::warning,
	  .id = 4,
	  .text = "Triggers require a collider",
	};

	if (std::ranges::any_of(children(), isShapeCollider)) {
		removeInspectorMessage(message);
	} else {
		addInspectorMessage(message);
	}
}

auto Trigger::getCurrentCollisions() const -> std::vector<toast::Box<toast::Node3D>> {
	std::vector<toast::Box<toast::Node3D>> result;
	result.reserve(m_current.size());
	for (const Overlap& overlap : m_current) {
		if (overlap.node.exists()) {
			result.push_back(overlap.node);
		}
	}
	return result;
}

auto Trigger::isInside(const toast::Node3D& node) const -> bool {
	return std::ranges::any_of(m_current, [&node](const Overlap& overlap) {
		return overlap.node.exists() && overlap.node.operator->() == &node;
	});
}

auto Trigger::empty() const -> bool {
	return m_current.empty();
}

void Trigger::enableCollider(bool value) {
	for (auto& c : children()) {
		if (auto shape = c.as<Collider>(); shape.exists()) {
			shape->disabled = !value;
		}
	}
}

void Trigger::begin() {
	if (participatesIn(toast::NodeOwnerParticipation::gameplay_tick)) {
		Simulator::registerTrigger(*this);
	}
	applyDebugColor();
}

void Trigger::end() {
	// everything still inside is leaving with us
	updateOverlaps({});
	Simulator::unregisterTrigger(*this);
}

void Trigger::onDisable() {
	// checkTriggers skips disabled triggers so the exits have to go out now
	updateOverlaps({});
}

void Trigger::applyDebugColor() {
	const glm::vec4 color = empty() ? empty_color : debug_color;
	for (const auto& child : children()) {
		if (auto sphere = child.as<SphereCollider>(); sphere.exists()) {
			sphere->overrideColor(color);
		} else if (auto box = child.as<BoxCollider>(); box.exists()) {
			box->overrideColor(color);
		} else if (auto capsule = child.as<CapsuleCollider>(); capsule.exists()) {
			capsule->overrideColor(color);
		}
	}
}

void Trigger::updateOverlaps(std::span<const BodyID> bodies) {
	// Settle the state before firing
	std::vector<toast::Box<toast::Node3D>> exited;
	std::vector<toast::Box<toast::Node3D>> entered;

	std::erase_if(m_current, [&](const Overlap& overlap) {
		if (std::ranges::find(bodies, overlap.body) != bodies.end() && overlap.node.exists()) {
			return false;
		}
		if (overlap.node.exists()) {
			exited.push_back(overlap.node);
		}
		return true;
	});

	for (const BodyID body : bodies) {
		if (std::ranges::find(m_current, body, &Overlap::body) != m_current.end()) {
			continue;
		}
		const auto node = Simulator::nodeFor(body).as<toast::Node3D>();
		if (not node.exists()) {
			continue;
		}
		m_current.push_back(Overlap {.body = body, .node = node});
		entered.push_back(node);
	}

	applyDebugColor();

	for (const auto& node : exited) {
		on_exit.fire(node);
		call("onExit", node);
	}
	for (const auto& node : entered) {
		on_enter.fire(node);
		call("onEnter", node);
	}
}

}
