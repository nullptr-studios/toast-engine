/// @file Object.hpp
/// @author Xein
/// @date 30/05/25
/// @brief Base Object Class

#pragma once

#include "../RTTIMacros.h"
#include "Engine/Core/Log.hpp"
#include "Engine/Toast/Factory.hpp"
#include "Engine/Toast/ISerializable.hpp"

#include <ranges>
#include <string_view>
#include <utility>

namespace toast {
class Actor;
class Scene;
class Component;

enum BaseType : uint8_t {
	ActorT = 0,
	ComponentT = 1,
	SceneT = 2,
	InvalidT = 3
};

class Object : public ISerializable {
	friend class World;
	friend class Engine;
	friend class Actor;

public:
#pragma region Children
#include "Children.inl"

	Children children;

	static void Register(const std::string& name, FactoryFunction func) {
		getRegistry()[name] = std::move(func);
	}

	template<typename T>
	struct Registrar {
		Registrar() {
			Object::Register(T::static_type(), [](Children& children, std::optional<unsigned> id) {
				return children._CreateObject<T>(id);
			});
		}
	};

#pragma endregion

	Object() = default;
	virtual ~Object() = default;

	Object(const Object&) = delete;
	Object& operator=(const Object&) = delete;
	Object(Object&&) = delete;
	Object& operator=(Object&&) = delete;

	// Serialize
	[[nodiscard]]
	json_t Save() const override;
	void Load(json_t j, bool force_create = true) override;

	// Soft-serialize
	void SoftLoad() override;
	void SoftSave() const override;

#ifdef TOAST_EDITOR
	void Inspector() override { }
#endif

	// Get/Set
	/// Returns the id of the object
	[[nodiscard]]
	unsigned id() const noexcept {
		return m_id;
	}

	[[nodiscard]]
	Object* parent() const noexcept {
		return m_parent;
	}

	[[nodiscard]]
	Scene* scene() const noexcept {
		return m_scene;
	}

	/// Returns the name of the object
	[[nodiscard]]
	const std::string& name() const noexcept {
		return m_name;
	}

	/// Sets the name of the object
	void name(std::string&& name) noexcept {
		m_name = std::move(name);
	}

	/// Returns the name of the class
	[[nodiscard]]
	static const char* static_type() {
		return "Object";
	}

	/// Returns the name of the class
	[[nodiscard]]
	virtual const char* type() const noexcept {
		return static_type();
	}

	[[nodiscard]]
	constexpr virtual BaseType base_type() const noexcept {
		return InvalidT;
	}

	/// Gets whether the object is enabled or not
	[[nodiscard]]
	bool enabled() const noexcept;
	/// An object that is not enabled will not be ticked and its components will be treated as disabled as well
	void enabled(bool enabled);

	[[nodiscard]]
	bool& enabled_ref() {
		return m_enabled;
	}

	[[nodiscard]]
	bool has_run_begin() const noexcept {
		return m_hasRunBegin;
	}

	void RefreshBegin(bool propagate);

	/// @brief Destroys this object
	void Nuke();

protected:
	// Tick functions
	/// @brief This function runs just after the object is created (scene load thread)
	virtual void Init() { }

	/// @brief This function runs when the scene starts or the next frame the object is created
	virtual void Begin() { }

	virtual void LoadTextures() { }

	/// @brief This function runs every frame before the tick
	virtual void EarlyTick() { }

	/// @brief This function runs every frame
	virtual void Tick() { }

	/// @brief This function runs every frame after the tick
	virtual void LateTick() { }

	/// @brief This function runs when the object is set to be destroyed
	virtual void Destroy() { }

	/// @brief This function runs every physics tick (physics thread)
	virtual void PhysTick() { }

	/// @brief This function runs when the object is enabled after being disabled
	virtual void OnEnable() { }

	/// @brief This function runs when the object is disabled
	virtual void OnDisable() { }

	/// @brief This function runs every frame in the editor
	virtual void EditorTick() { }

private:
	unsigned m_id = -1;
	std::string m_name;
	bool m_enabled = false;
	Object* m_parent = nullptr;
	Scene* m_scene = nullptr;

	// Makes objects not be able to be ticked without having run the begin function
	std::atomic_bool m_hasRunBegin = false;
	std::atomic_bool m_hasBeenDestroyed = false;

	// Stores json with the init parameters for soft-reloading
	mutable json_t m_json;

#pragma region Accessors
	// This functions need to exist in order to properly implement top-left traversal in our tree structure
	// The engine is ment to call them rather than the public methods as they will also call the child's
	// functions if they exist. For everything else, disregard them.
	void _Init();
	void _Begin(bool propagate = false);
	void _EarlyTick();
	void _Tick();
	void _EditorTick();
	void _LateTick();
	void _Destroy();
	void _PhysTick();
	void _OnEnable();
	void _OnDisable();
	void _enabled(bool enabled);

public:
	void _LoadTextures();

private:
#pragma endregion
};

#include "Object.inl"

}
