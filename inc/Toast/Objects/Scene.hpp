/// @file Scene.hpp
/// @date 6/2/2025
/// @author Xein
/// @brief A collection of Actors

#pragma once
#include "Object.hpp"

namespace toast {
struct ActorLookup;
class ChildMap;

/// @class toast::Scene
/// @brief A collection of Actors
/// The scene manages its Actor children with the ChildMap abstraction @see toast::ChildMap
///
/// A scene has a unique ID and a name that allows it to be searched in the World @see toast::World
class Scene : public Object {
public:
	REGISTER_TYPE(Scene);

	Scene() {
		enabled(false);
	}

	~Scene() override = default;

	// Serialization
	[[nodiscard]]
	json_t Save() const override;
	void Load(const std::string& json_path);

	const std::string& json_path() const {
		return m_jsonPath;
	}

	void json_path(const std::string& path) const {
		m_jsonPath = path;
	}


private:
	// Scenes should pass the path rather than the object
	void Load(json_t j, bool force_create = true) override;
	mutable std::string m_jsonPath;


	constexpr BaseType base_type() const noexcept final {
		return SceneT;
	}

protected:
	void Init() override { }

	void Begin() override { }

	void EarlyTick() override { }

	void Tick() override { }

	void LateTick() override { }

	void PhysTick() override { }

public:
	void Restart();
};

}
