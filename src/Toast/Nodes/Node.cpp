#include "Toast/Nodes/Node.hpp"

#include "Toast/Log.hpp"
#include "Toast/World.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <ranges>

namespace toast {
void Node::Load(json_t j, const bool force_create) {
	PROFILE_ZONE;

	if (!m_serialize) {
		return;
	}

	// Verify that the type for the object is the same as in the json
	if (type() != j["type"].get<std::string>()) {
		TOAST_ERROR("Trying to serialize for object {0} as type {1}. Expected {2}", name(), j["type"].get<std::string>(), type());
		return;
	}

	std::string name = j["name"];
	std::ranges::replace(name, ' ', '_');
	m_name = name;
	m_enabled = j["enabled"].get<bool>();

	m_json = j;    // Store for soft reloads

	for (auto& json_c : j["children"]) {
		if (!json_c.contains("type")) {    // SKIP NOT ACTUAL CHILD ENTRIES
			continue;
		}

		const auto type = json_c["type"].get<std::string>();
		std::string c_name = json_c["name"];

		if (force_create) {
			// Always create a new child without checking if it exists (default)
			children.Add(type, c_name, json_c);
			continue;
		}

		// Check if the child already exists by name
		if (children.Has(c_name)) {
			children.Get(c_name)->Load(json_c, false);
		} else {
			// If not, we still create a new one
			children.Add(type, c_name, json_c);
		}
	}
}

json_t Node::Save() const {
	PROFILE_ZONE;

	json_t j {};

	if (!m_serialize) {
		return j;
	}

	j["type"] = type();
	j["name"] = name();
	j["enabled"] = enabled();

	json_t j_children {};
	size_t index = 0;
	for (const auto& c : children | std::views::values) {
		j_children[index] = c->Save();
		index++;
	}
	j["children"] = j_children;

	return j;
}

void Node::SoftLoad() {
	// you shouldn't need to propagate this function
	Load(m_json, false);
}

void Node::SoftSave() const {
	m_json = Save();

	for (const auto& child : children | std::views::values) {
		child->SoftSave();
	}
}

bool Node::enabled() const noexcept {
	return m_enabled;
}

void Node::enabled(bool enabled) {
	// if (m_enabled == enabled) {
	// 	return;
	// }

	if (enabled) {
		_OnEnable();
	} else {
		_OnDisable();
	}
	for (const auto& child : children | std::views::values) {
		// This function propagates the enabled state to children
		// However, when enabling, it reads from the json to restore previous state
		child->_enabled(enabled);
	}

	m_enabled = enabled;
	m_json["enabled"] = m_enabled;
}

void Node::RefreshBegin(const bool propagate) {
	if (m_hasRunBegin) {
		// If it has run begin, we need to re-schedule it
		m_hasRunBegin = false;
		World::ScheduleBegin(this);
	} else {
		// If it hasn't run begin, check if it's on the Begin queue and if not,
		// schedule it again
		const auto& bq = World::Instance()->begin_queue();
		if (std::ranges::find(bq, this) == bq.end()) {
			World::ScheduleBegin(this);
		}
	}

	if (propagate) {
		for (const auto& [_, c] : children) {
			c->RefreshBegin(propagate);
		}
	}
}

void Node::Nuke() {
	// Don't make this function const even if it's possible
	if (parent()) {
		parent()->children.Remove(id());
	} else {
		if (base_type() != RootNodeT) {
			TOAST_ERROR("Trying to nuke \"{0}\" but the bomb doesn't have enough uranium, ask Xein for more uranium", name());
			// This will probably crash the engine, but maybe not
			const_cast<Children&>(World::Instance()->GetChildren()).Remove(id());
			return;
		}
		TOAST_WARN("RootNode \"{0}\" (id {1}) was nuked", name(), id());
		World::UnloadRootNode(id());
	}
}

#pragma region Children stuff

RootNode* Node::Children::scene() const {
	if (!m_scene.has_value()) {
		// 	throw ToastException("RootNode has not been set");
		TOAST_ERROR("scene() has not been set");
		return nullptr;
	}
	return m_scene.value();
}

Node* Node::Children::parent() const {
	if (!m_parent.has_value()) {
		// 	throw ToastException("Parent has not been set");
		TOAST_ERROR("parent() has not been set");
		return nullptr;
	}
	return m_parent.value();
}

bool Node::Children::Has(const unsigned id) const {
	// Check if the id exists in the map and is not nullptr
	const auto it = m_children.find(id);
	return it != m_children.end() && it->second != nullptr;
}

bool Node::Children::Has(std::string_view name) const {
	for (const auto& child : m_children | std::views::values) {
		if (child->name() == name) {
			return true;
		}

		// Propagate down the tree
		if (child->children.Has(name)) {
			return true;
		}
	}

	return false;
}

bool Node::Children::HasType(std::string_view type, bool propagate) const {
	for (const auto& child : m_children | std::views::values) {
		if (child->type() == type) {
			return true;
		}
		if (!propagate) {
			continue;
		}

		// Propagate down the tree
		if (child->children.HasType(type, propagate)) {
			return true;
		}
	}

	return false;
}

Node* Node::Children::Get(unsigned id) {
	if (const auto& find = m_children.find(id); find != m_children.end()) {
		return find->second.get();
	}

	if (m_children.empty()) {
		return nullptr;
	}

	// Recursively search
	for (const auto& child : m_children | std::views::values) {
		if (Node* result = child->children.Get(id); result != nullptr) {
			return result;
		}
	}

	return nullptr;
}

Node* Node::Children::Get(std::string_view name) {
	for (const auto& child : m_children | std::views::values) {
		// Check for child name
		if (child->name() == name) {
			return child.get();
		}

		// Recursively search
		if (Node* result = child->children.Get(name); result != nullptr) {
			return result;
		}
	}

	// Not found -> return nullptr
	// TOAST_WARN("Node with name '{0}' not found", name);
	return nullptr;
}

Node* Node::Children::GetType(std::string_view type, bool propagate) {
	for (const auto& child : m_children | std::views::values) {
		if (child->type() == type) {
			return child.get();
		}
		if (!propagate) {
			continue;
		}

		if (auto* o = child->children.GetType(type, propagate); o) {
			return o;
		}
	}

	return nullptr;
}

Node::Children::child_list& Node::Children::GetAll() {
	return m_children;
}

Node* Node::Children::operator[](const unsigned id) {
	return this->Get(id);
}

Node* Node::Children::operator[](std::string_view name) {
	return this->Get(name);
}

Node* Node::Children::Add(std::string_view type, std::optional<std::string_view> name, std::optional<json_t> file) {
	auto registry = getRegistry();
	if (!registry.contains(type.data())) {
		TOAST_ERROR("Type {0} not found in registry", type);
		return nullptr;
	}

	auto* obj = registry[type.data()](*this, std::nullopt);
	_ConfigureNode(obj, name, file);
	return obj;
}

void Node::Children::_ConfigureNode(Node* obj, const std::optional<std::string_view>& name, const std::optional<json_t>& file) const {
	// Go to fallback name if not provided
	if (name.has_value()) {
		obj->m_name = *name;
	} else {
		obj->m_name = std::format("{0}_{1}", obj->type(), obj->id());
	}

	// Set object parent() and scene()
	obj->m_parent = parent();
	obj->m_scene = scene();
	obj->children.parent(obj);
	obj->children.scene(scene());

	// If a file was provided, deserialize first
	if (file.has_value()) {
		obj->Load(*file);
	}

	// Run initialization
	obj->_Init();
	if (!file.has_value()) {
		obj->enabled(true);
	}

	// Add to begin queue
	World::ScheduleBegin(obj);
}

void Node::Children::Remove(unsigned id) {
	if (auto* o = Get(id); o != nullptr) {
		// Run the destroy logic
		o->_Destroy();
		// And schedule it for destruction
		World::ScheduleDestroy(m_children[id].get());
		return;
	}

	// If not found, propagate down the tree
	for (const auto& [_, child] : m_children) {
		child->children.Remove(id);
	}
}

void Node::Children::Remove(std::string_view name) {
	for (const auto& [_, child] : m_children) {
		if (child->name() == name) {
			// run the destroy logic
			child->_Destroy();
			// And schedule it for destruction
			World::ScheduleDestroy(child.get());
			return;
		}

		// If not found, propagate down the tree
		child->children.Remove(name);
	}
}

void Node::Children::RemoveAll() {
	for (const auto& [_, c] : m_children) {
		World::ScheduleDestroy(c.get());
	}
}

// Here lies the remnents of dante code :(
// auto Node::Children::RecursiveLoop() -> std::generator<Children&> {
// 	for (auto& child : m_children | std::views::values) {
// 		co_yield child->children;
// 		co_yield std::ranges::elements_of(child->children.RecursiveLoop());
// 	}
// }
// omg dante -x

void Node::SetRootNode(RootNode* scene) {
	assert(scene != nullptr);
	m_scene = scene;
	children.m_scene = scene;
	for (auto& child : children | std::views::values) {
		child->SetRootNode(scene);
	}
}

auto Node::Children::Collect(const unsigned id) -> std::unique_ptr<Node> {
	auto iter = m_children.find(id);
	assert(iter != m_children.end());
	auto ptr = std::move(iter->second);
	m_children.erase(iter);
	return ptr;
}

void Node::Adopt(unsigned id) {
	assert(id != m_id);
	// adopting parent test
	Node* iter;    // NOLINT
	if ((iter = parent())) {
		while (iter->id() != id && (iter = iter->parent())) { }
		if (iter) {
			TOAST_ERROR("adopting one's own parent not allowed");
			return;
		}
	}
	//
	auto* obj = iter ? iter : World::Get(id);
	if (!obj) {
		TOAST_ERROR("Cannot Adopt Child When Child Does Not Exist");
		return;
	}
	auto parent = (obj->parent() ? std::optional<Node*>(obj->parent()) : std::nullopt);
	Children* children_of_parent;    // NOLINT
	if (parent) {
		children_of_parent = &parent.value()->children;
	} else {
		children_of_parent = &World::GetChildren();
	}
	auto orphan = children_of_parent->Collect(id);

	orphan->m_parent = this;
	orphan->SetRootNode(m_scene);
	children.m_children[id] = std::move(orphan);
}

#pragma endregion

void Node::_Init() {
	PROFILE_ZONE_C(0xFF6B00);    // Orange for initialization
	PROFILE_TEXT(type(), strlen(type()));

	Init();    // ACTOR's LOGIC
}

void Node::_Begin(bool propagate) {
	if (!enabled()) {
		return;
	}
	PROFILE_ZONE_C(0xFFFF00);    // Yellow for begin
	PROFILE_TEXT(type(), strlen(type()));

	Begin();    // ACTOR's LOGIC

#ifndef TOAST_EDITOR
	LoadTextures();
#endif

	m_hasRunBegin = true;

#ifndef TOAST_EDITOR
	static_cast<void>(propagate);
#else
	if (propagate) {
		for (const auto& child : children | std::views::values) {
			child->_Begin(propagate);
		}
	}
#endif
}

void Node::_EarlyTick() {
	if (not m_runsEarlyTick) {
		return;
	}

	if (!enabled() || !m_hasRunBegin) {
		return;
	}

	PROFILE_ZONE_C(0x00FF80);    // Light green for early tick
	PROFILE_TEXT(type(), strlen(type()));

	EarlyTick();    // ACTOR's LOGIC

	// Then continue to the children
	for (const auto& child : children | std::views::values) {
		child->_EarlyTick();
	}
}

void Node::_Tick() {
	if (!enabled() || !m_hasRunBegin || !m_runsTick) {
		return;
	}

	PROFILE_ZONE_C(0x00FF00);    // Green for main tick
	PROFILE_TEXT(type(), strlen(type()));

	Tick();    // ACTOR's LOGIC

	// Then continue to the children
	for (const auto& child : children | std::views::values) {
		child->_Tick();
	}
}

#ifdef TOAST_EDITOR
void Node::_EditorTick() {
	if (not m_runsLateTick) {
		return;
	}

	if (!enabled()) {
		return;
	}
	PROFILE_ZONE_C(0xFF00FF);    // Magenta for editor tick
	PROFILE_TEXT(type(), strlen(type()));

	EditorTick();

	// Then continue to the children
	for (const auto& child : children | std::views::values) {
		child->_EditorTick();
	}
}
#endif

void Node::_LateTick() {
	if (!enabled() || !m_hasRunBegin) {
		return;
	}

	PROFILE_ZONE_C(0x80FF00);    // Yellow-green for late tick
	PROFILE_TEXT(type(), strlen(type()));

	LateTick();    // ACTOR's LOGIC

	// Then continue to the children
	for (const auto& child : children | std::views::values) {
		child->_LateTick();
	}
}

void Node::_Destroy() {
	// Commenting this because the destroy function should run always
	// even if the object is destroyed while disabled
	// if (!enabled()) return;

	if (m_hasBeenDestroyed) {
		return;
	}

	m_hasBeenDestroyed = true;

	PROFILE_ZONE_C(0xFF0000);    // Red for destruction
	PROFILE_TEXT(type(), strlen(type()));

	// Check if we need to de-schedule its Begin
	World::CancelBegin(this);

	Destroy();    // ACTOR's LOGIC

	// Then continue to the children
	for (const auto& child : children | std::views::values) {
		child->_Destroy();
	}
}

void Node::_PhysTick() {
	if (!enabled() || !m_hasRunBegin || !m_runsPhysTick) {
		return;
	}

	PROFILE_ZONE_C(0xFF8000);    // Orange for physics tick
	PROFILE_TEXT(type(), strlen(type()));

	PhysTick();    // ACTOR's LOGIC

	// Then continue to the children
	for (const auto& child : children | std::views::values) {
		child->_PhysTick();
	}
}

void Node::_OnEnable() {
	OnEnable();
}

void Node::_OnDisable() {
	OnDisable();
}

void Node::_enabled(const bool enabled) {
	if (enabled && !m_json.empty()) {
		if (m_json.contains("enabled")) {
			m_enabled = m_json["enabled"];
		} else {
			return;    // EARLY EXIT IF NOT VALID OBJ ENTRY
		}
	} else {
		m_enabled = false;
	}

	for (const auto& child : children | std::views::values) {
		child->_enabled(enabled);
	}
}

void Node::_LoadTextures() {
	PROFILE_ZONE_C(0xFFFF00);    // Yellow for texture loading
	PROFILE_TEXT(type(), strlen(type()));

	LoadTextures();

	// Then continue to the children
	for (const auto& child : children | std::views::values) {
		child->_LoadTextures();
	}
}
}
