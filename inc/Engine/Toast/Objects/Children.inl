/// @file Children.inl
/// @author Xein
/// @date 09/11/25

#pragma once

class Children;

// Changed FactoryFunction to accept a run_init flag so callers can create prototype objects without running Init.
using FactoryFunction = std::function<Object*(Children&, std::optional<unsigned>)>;

static std::unordered_map<std::string, FactoryFunction>& getRegistry() {
	static std::unordered_map<std::string, FactoryFunction> instance;
	return instance;
}

class Children {
	friend class Object;
	friend class World;
	using child_list = std::unordered_map<unsigned, std::unique_ptr<Object>>;

public:
	Children() = default;
	~Children() = default;

	// Get ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// @brief Search an Actor and cast it to the correct class
	/// Getting a direct child by ID is O(1) while propagating down is O(n)
	/// @tparam T Class to do the casting to
	/// @param id Target Actor to find
	template<typename T>
	[[nodiscard]]
	T* Get(unsigned id);

	/// @brief Search an actor and cast it to the correct class
	/// Getting a child by name is O(n)
	/// @tparam T Class to do the casting to
	/// @param name Name of the target Actor to find
	template<typename T>
	[[nodiscard]]
	T* Get(const std::string& name);

	template<typename T>
	[[nodiscard]]
	T* Get();

	/// @brief Search an Actor
	/// Getting a direct child by ID is O(1) while propagating down is O(n)
	/// @param id Target Actor to find
	[[nodiscard]]
	Object* Get(unsigned id);

	/// @brief Search an Actor
	/// Getting a child by name is O(n)
	/// @param name Name of the target Actor to find
	[[nodiscard]]
	Object* Get(const std::string& name);

	/// @brief Search a child by type passed as string
	/// @param propagate Propagate down the tree (no by default)
	[[nodiscard]]
	Object* GetType(const std::string& type, bool propagate = false);

	/// @brief Returns a reference to the raw list
	[[nodiscard]]
	child_list& GetAll();

	/// @brief Search an Actor
	/// Getting a direct child by ID is O(1) while propagating down is O(n)
	/// @param id Target Actor to find
	[[nodiscard]]
	Object* operator[](unsigned id);

	/// @brief Search an Actor
	/// Getting a child by name is O(n)
	/// @param name Name of the target Actor to find
	[[nodiscard]]
	Object* operator[](const std::string& name);

	// Has ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	template<typename... Components>
	[[nodiscard]]
	bool Has();

	/// @brief Check whether a child exists or not
	/// @note This function does not propagate down
	/// Checking if an Actor exists by ID is O(1)
	/// @param id Target Actor to find
	[[nodiscard]]
	bool Has(unsigned id) const;

	/// @brief Check whether a child exists or not
	/// Checking if an Actor exists by name is O(n)
	/// @param name Name of the target Actor to find
	[[nodiscard]]
	bool Has(const std::string& name) const;

	/// @brief Check whether a child of a type exists or not
	/// Checking if a type exists by string is O(n)
	/// @param propagate tells if the function should propagate down the tree or not (no by default)
	bool HasType(const std::string& type, bool propagate = false) const;

	// Add ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// @brief Creates a new child
	/// @tparam T Class to create the child from
	/// @param name Initial name for the child
	/// @warning If you are creating an object on the Init() consider TryAdd()
	/// @see TryAdd
	// Added run_init param so callers can avoid running _Init when creating preview/ghost objects
	template<typename T>
	T* Add(std::optional<std::string_view> name = {}, std::optional<json_t> file = {});

	Object* Add(std::string type, std::optional<std::string_view> name = {}, std::optional<json_t> file = {});

	/// @brief Creates a new child if the child doesnt exist
	/// @tparam T Class to create the child from
	/// @param name Initial name for the child
	///
	/// If the child trying to create exists, it will return the Get<> of that object rather than creating a new one
	template<typename T>
	T* AddRequired(std::optional<std::string_view> name = {}, std::optional<json_t> file = {});

	/// @brief Basic object creation
	/// @warning This function exists to bypass traditional object creation. IT IS NOT MENT TO BE CALLED BY THE CLIENT
	///
	/// This function just creates a uptr and emplaces to the list. If this object is used AS-IS, it will most likely
	/// crash the application at some point
	template<typename T>
	T* _CreateObject(std::optional<unsigned> id);
	void _ConfigureObject(Object* obj, const std::optional<std::string_view>& name = {}, const std::optional<json_t>& file = {}) const;

public:
	// Remove /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	template<typename T>
	void Remove();

	/// @brief Removes children by ID
	/// @param id Target ID to remove
	void Remove(unsigned id);

	/// @brief Removes children by name
	/// @param name Target name to remove
	void Remove(const std::string& name);

	/// @brief Adds all the objects to the destroy queue
	void RemoveAll();

	// Misc ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	/// @brief Returns the number of children
	[[nodiscard]]
	unsigned size() const {
		return m_children.size();
	}

	[[nodiscard]]
	Scene* scene() const;

	void scene(Scene* scene) {
		m_scene = scene;
	}

	[[nodiscard]]
	Object* parent() const;

	void parent(Object* parent) {
		m_parent = parent;
	}

	child_list::iterator begin() {
		return m_children.begin();
	}

	child_list::iterator end() {
		return m_children.end();
	}

	child_list::const_iterator begin() const {
		return m_children.begin();
	}

	child_list::const_iterator end() const {
		return m_children.end();
	}

private:
	template<typename T>
	bool HasObject();

	void erase(const unsigned id) {
		m_children.erase(id);
	}

	child_list m_children;

	std::optional<Object*> m_parent;
	std::optional<Scene*> m_scene;
};
