#include "play_workspace.hpp"

#include "node.hpp"
#include "workspace_events.hpp"

#include <toast/assets/assets.hpp>
#include <toast/log.hpp>

namespace toast {

PlayWorkspace::PlayWorkspace(UID handle, assets::Prefab& prefab) : Workspace(handle, EmptyTag {}) {
	ZoneScoped;

	assets::Handle<assets::Prefab> file(&prefab, handle, "");

	INodeOwner::InstantiateContext ctx;
	ctx.resolver = [](toast::UID id) { return assets::load<assets::Prefab>(id); };
	Box<Node> node = instantiate(file, ctx);
	if (not node.exists()) {
		TOAST_ERROR("World", "Failed to instantiate play workspace {}", m_handle);
		return;
	}
	// node->propagateCallTick(node->info(), TickFunctionList::load);
	// node->propagateCallTick(node->info(), TickFunctionList::pre_init);

	node->m_parent = {};
	node->changeNodeState(NodeState::root);
	node->m_type = NodeType::world_root;
	node->m_inherited_enabled = true;

	node->propagateCallTick(node->info(), TickFunctionList::init);
	node->propagateCallTick(node->info(), TickFunctionList::begin);
	node->m_local_enabled = true;
	node->propagateEnable();

	m_root_node = node;

	m_listener.subscribe<event::WorkspacePause>([this](const auto& e) {
		if (e.handle != m_handle.data()) {
			return false;
		}
		m_paused = e.paused;
		TOAST_INFO("World", "Play workspace {} {}", m_root_node->name(), m_paused ? "paused" : "resumed");
		return true;
	});

	m_listener.subscribe<event::RequestHierarchyUpdate>([this] {
		m_schedule_dirty = true;
		return false;
	});

	TOAST_INFO("World", "Created play workspace from {}", m_root_node->name());
}

PlayWorkspace::~PlayWorkspace() {
	if (not m_root_node.exists()) {
		return;
	}

	TOAST_INFO("World", "Destroyed play workspace {}", m_root_node->name());
}

auto PlayWorkspace::name() -> std::string {
	return Workspace::name();
}

void PlayWorkspace::registerDependency(Node& from, Node& to) {
	m_scheduler.registerDependency(from, to);
	m_schedule_dirty = true;
}

void PlayWorkspace::unregisterDependency(Node& from, Node& to) {
	m_scheduler.unregisterDependency(from, to);
	m_schedule_dirty = true;
}

void PlayWorkspace::tick() {
	if (participatesIn(NodeOwnerParticipation::gameplay_tick) && !m_paused && m_root_node.exists()) {
		if (m_schedule_dirty) {
			computeSchedule();
			m_schedule_dirty = false;
		}

		m_scheduler.runPhase(m_scheduler.schedule.early_tick, TickFunctionList::early_tick, "early_tick");
		INodeOwner::updateTransforms(*m_root_node);
		m_scheduler.runPhase(m_scheduler.schedule.tick, TickFunctionList::tick, "tick");
		// TODO: physics step goes between tick and post_physics
		m_scheduler.runPhase(m_scheduler.schedule.post_physics, TickFunctionList::post_physics, "post_physics");
		m_scheduler.runPhase(m_scheduler.schedule.late_tick, TickFunctionList::late_tick, "late_tick");
	}

	if (isActiveWorkspace()) {
		Workspace::tick();
	}
}

auto PlayWorkspace::participatesIn(NodeOwnerParticipation /*use*/) const noexcept -> bool {
	return isActiveWorkspace();
}

void PlayWorkspace::computeSchedule() {
	ZoneScoped;

	std::vector<Box<Node>> all_nodes;
	{
		std::scoped_lock lock(nodes_mutex);
		forEachNode([&](const _detail::ControlBox& node) {
			if (!node.node || node.node->m_state == NodeState::destroy) {
				return;
			}
			all_nodes.emplace_back(node.node->box());
		});
	}

	m_scheduler.compute(all_nodes);
}

}
