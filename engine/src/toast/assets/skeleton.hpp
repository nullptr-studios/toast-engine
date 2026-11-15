/**
 * @file skeleton.hpp
 * @author dario
 * @date 13 Nov 2026
 */

#pragma once
#include "core_types.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <span>
#include <string>
#include <toast/export.hpp>
#include <vector>

namespace assets {

namespace _detail {
inline constexpr uint32_t skeleton_format_version = 1;
}

/// Local bone transform, trivially copyable since .tskel writes it raw
struct BonePose {
	glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
	glm::vec3 translation {0.0f};
	glm::vec3 scale {1.0f};
};

struct TOAST_API Bone {
	std::string name;
	int32_t parent = -1;    ///< always below this bone index
	BonePose rest;
};

/// Joint names and inverse bind matrices a skinned mesh binds with
struct TOAST_API Skin {
	std::string name;
	std::string skeleton_root;                       ///< node name, empty when glTF gives none
	std::vector<std::string> joints;                 ///< bone names in the order vertex joint indices use
	std::vector<glm::mat4> inverse_bind_matrices;    ///< parallel to joints, empty means identity for all

	/// `joint_world * inverse_bind` per joint, a missing world entry counts as identity so the size always matches
	[[nodiscard]]
	auto jointMatrices(std::span<const glm::mat4> joint_world_transforms) const -> std::vector<glm::mat4>;
};

/// Bone hierarchy (.tskel) posed by toast::SkeletalNode, plus the skins bound to it
class TOAST_API Skeleton : public Asset {
public:
	explicit Skeleton(std::span<const uint8_t> data);

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "skeleton";
	}

	[[nodiscard]]
	auto name() const noexcept -> const std::string& {
		return m_name;
	}

	/// Parent always precedes child
	[[nodiscard]]
	auto bones() const noexcept -> const std::vector<Bone>& {
		return m_bones;
	}

	[[nodiscard]]
	auto skins() const noexcept -> const std::vector<Skin>& {
		return m_skins;
	}

	/// @returns bone index or -1
	[[nodiscard]]
	auto find(std::string_view bone_name) const -> int32_t;

	/// @returns the skin with this name, the first one for an empty name, or nullptr
	[[nodiscard]]
	auto findSkin(std::string_view skin_name) const -> const Skin*;

	[[nodiscard]]
	static auto toBinary(std::string_view name, std::span<const Bone> bones, std::span<const Skin> skins) -> std::vector<uint8_t>;

private:
	std::string m_name;
	std::vector<Bone> m_bones;
	std::vector<Skin> m_skins;
};

}
