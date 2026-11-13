#include "skeleton.hpp"

#include "binary_io.hpp"

namespace assets {

namespace {

constexpr std::array<uint8_t, 4> k_magic {'T', 'S', 'K', 'L'};

}

auto Skin::jointMatrices(std::span<const glm::mat4> joint_world_transforms) const -> std::vector<glm::mat4> {
	ZoneScoped;
	std::vector<glm::mat4> matrices(joints.size(), glm::mat4(1.0f));
	for (size_t i = 0; i < joints.size(); ++i) {
		const glm::mat4 world = i < joint_world_transforms.size() ? joint_world_transforms[i] : glm::mat4(1.0f);
		const glm::mat4 inverse_bind = i < inverse_bind_matrices.size() ? inverse_bind_matrices[i] : glm::mat4(1.0f);
		matrices[i] = world * inverse_bind;
	}
	return matrices;
}

Skeleton::Skeleton(std::span<const uint8_t> data) {
	ZoneScoped;
	_detail::Reader reader(data);

	if (reader.read<std::array<uint8_t, 4>>() != k_magic) {
		TOAST_ERROR("Animation", "Not a .tskel file (bad magic)");
		return;
	}
	const auto version = reader.read<uint32_t>();
	if (version != _detail::skeleton_format_version) {
		TOAST_ERROR(
		    "Animation", "Unsupported .tskel format version {} (this build reads {})", version, _detail::skeleton_format_version
		);
		return;
	}

	m_name = reader.readString();
	const auto bone_count = reader.read<uint32_t>();
	m_bones.reserve(bone_count);
	for (uint32_t b = 0; b < bone_count && !reader.overflowed(); ++b) {
		Bone bone;
		bone.name = reader.readString();
		bone.parent = reader.read<int32_t>();
		bone.rest = reader.read<BonePose>();
		// Parent below child is what lets SkeletalNode pose in one forward pass
		if (bone.parent >= static_cast<int32_t>(b)) {
			bone.parent = -1;
		}
		m_bones.push_back(std::move(bone));
	}

	const auto skin_count = reader.read<uint32_t>();
	m_skins.reserve(skin_count);
	for (uint32_t s = 0; s < skin_count && !reader.overflowed(); ++s) {
		Skin skin;
		skin.name = reader.readString();
		skin.skeleton_root = reader.readString();
		const auto joint_count = reader.read<uint32_t>();
		skin.joints.reserve(joint_count);
		for (uint32_t j = 0; j < joint_count && !reader.overflowed(); ++j) {
			skin.joints.push_back(reader.readString());
		}
		skin.inverse_bind_matrices = reader.readArray<glm::mat4>();
		m_skins.push_back(std::move(skin));
	}

	if (reader.overflowed()) {
		TOAST_ERROR("Animation", "Truncated .tskel file '{}'", m_name);
	}
}

auto Skeleton::find(std::string_view bone_name) const -> int32_t {
	for (size_t i = 0; i < m_bones.size(); ++i) {
		if (m_bones[i].name == bone_name) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}

auto Skeleton::findSkin(std::string_view skin_name) const -> const Skin* {
	if (skin_name.empty()) {
		return m_skins.empty() ? nullptr : &m_skins.front();
	}
	for (const auto& skin : m_skins) {
		if (skin.name == skin_name) {
			return &skin;
		}
	}
	return nullptr;
}

auto Skeleton::toBinary(std::string_view name, std::span<const Bone> bones, std::span<const Skin> skins) -> std::vector<uint8_t> {
	ZoneScoped;
	_detail::Writer writer;
	writer.write(k_magic);
	writer.write(_detail::skeleton_format_version);
	writer.writeString(std::string(name));
	writer.write(static_cast<uint32_t>(bones.size()));
	for (const auto& bone : bones) {
		writer.writeString(bone.name);
		writer.write(bone.parent);
		writer.write(bone.rest);
	}
	writer.write(static_cast<uint32_t>(skins.size()));
	for (const auto& skin : skins) {
		writer.writeString(skin.name);
		writer.writeString(skin.skeleton_root);
		writer.write(static_cast<uint32_t>(skin.joints.size()));
		for (const auto& joint : skin.joints) {
			writer.writeString(joint);
		}
		writer.writeArray(skin.inverse_bind_matrices);
	}
	return writer.take();
}

}
