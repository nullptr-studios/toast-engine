/// @file RootNode.hpp
/// @date 6/2/2025
/// @author Xein
/// @brief A collection of Node3Ds

#pragma once
#include "Node.hpp"

namespace toast {
struct Node3DLookup;
class ChildMap;

/// @class toast::RootNode
/// @brief A collection of Node3Ds
/// The scene manages its Node3D children with the ChildMap abstraction @see toast::ChildMap
///
/// A scene has a unique ID and a name that allows it to be searched in the World @see toast::World
class RootNode : public Node {
public:
	REGISTER_TYPE(RootNode);

	RootNode() {
		enabled(false);
	}

	~RootNode() override = default;

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

protected:
	// RootNodes should pass the path rather than the object
	void Load(json_t j, bool force_create = true) override;
	mutable std::string m_jsonPath;

	constexpr BaseType base_type() const noexcept final {
		return RootNodeT;
	}

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
