/// @file Object.inl
/// @author Xein
/// @date 09/11/25

#pragma once

template<typename T>
T* Object::Children::Get(const unsigned id) {
	static_assert(std::is_base_of_v<Object, T>, "T is not an Actor");
	return dynamic_cast<T*>(Get(id));
}

template<typename T>
T* Object::Children::Get(const std::string& name) {
	static_assert(std::is_base_of_v<Object, T>, "T is not an Actor");
	return dynamic_cast<T*>(Get(name));
}

template<typename T>
T* Object::Children::Get() {
	static_assert(std::is_base_of_v<Object, T>, "Object must derive from Object");
	for (const auto& child : m_children | std::views::values) {
		if (T* casted = dynamic_cast<T*>(child.get())) {
			return casted;
		}
	}

	// TOAST_WARN("Object of type {0} not found", T::static_type());
	return nullptr;
}

template<typename T>
bool Object::Children::HasObject() {
	static_assert(std::is_base_of_v<Object, T>, "T is not a component");
	return std::any_of(m_children.begin(), m_children.end(), [](const auto& c) {
		return c.second->type() == T::static_type();
	});
}

template<typename... Components>
bool Object::Children::Has() {
	return (HasObject<Components>() && ...);
}

template<typename T>
T* Object::Children::_CreateObject(std::optional<unsigned> id) {
	// Create an uptr and assign it an id
	std::unique_ptr<Object> obj = std::make_unique<T>();
	unsigned obj_id = 0;
	if (id.has_value()) {
		obj_id = *id;
	} else {
		obj_id = Factory::AssignId();
	}
	obj->m_id = obj_id;

	// Push it to the children list and return a rptr
	m_children[obj_id] = std::move(obj);
	return static_cast<T*>(m_children[obj_id].get());
}

template<typename T>
T* Object::Children::Add(std::optional<std::string_view> name, std::optional<json_t> file) {
	static_assert(std::is_base_of_v<Object, T>, "T is not an Actor");

	Object* obj = _CreateObject<T>(std::nullopt);
	_ConfigureObject(obj, name, file);
	return static_cast<T*>(obj);
}

template<typename T>
T* Object::Children::AddRequired(std::optional<std::string_view> name, std::optional<json_t> file) {
	if (auto* o = Get<T>()) {
		return o;
	}

	return Add<T>(std::move(name), std::move(file));
}

template<typename T>
void Object::Children::Remove() {
	static_assert(std::is_base_of_v<Object, T>, "T must derive from Component");
	auto it = m_children.begin();
	while (it != m_children.end() && it->second->type() != T::static_type()) {
		++it;
	}

	if (it == m_children.end()) {
		TOAST_WARN("Component of type {0} didn't exist on Actor {1}", T::static_type(), parent()->name());
		return;
	}

	it->second->Nuke();
}
