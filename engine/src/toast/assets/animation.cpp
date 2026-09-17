#include "animation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <glm/gtc/quaternion.hpp>
#include <toast/log.hpp>
#include <tracy/Tracy.hpp>
#include <utility>

namespace assets {

namespace {

/// @brief Bounds-checked sequential reader over the .tanim byte blob
///
/// Every read goes through here so a truncated or corrupt file fails on the read that runs off the end
/// rather than quietly returning garbage that only shows up as a broken pose much later
class Reader {
public:
	explicit Reader(std::span<const uint8_t> data) : m_data(data) { }

	template<typename T>
	auto read() -> T {
		static_assert(std::is_trivially_copyable_v<T>);
		if (m_offset + sizeof(T) > m_data.size()) {
			m_overflowed = true;
			return T {};
		}
		T value {};
		std::memcpy(&value, m_data.data() + m_offset, sizeof(T));
		m_offset += sizeof(T);
		return value;
	}

	auto readString() -> std::string {
		const auto length = read<uint32_t>();
		if (m_overflowed || m_offset + length > m_data.size()) {
			m_overflowed = true;
			return {};
		}
		std::string value(reinterpret_cast<const char*>(m_data.data() + m_offset), length);
		m_offset += length;
		return value;
	}

	/// @brief Reads a length-prefixed array of trivially copyable values
	template<typename T>
	auto readArray() -> std::vector<T> {
		const auto count = read<uint32_t>();
		if (m_overflowed || m_offset + (static_cast<size_t>(count) * sizeof(T)) > m_data.size()) {
			m_overflowed = true;
			return {};
		}
		std::vector<T> values(count);
		if (count > 0) {
			std::memcpy(values.data(), m_data.data() + m_offset, static_cast<size_t>(count) * sizeof(T));
			m_offset += static_cast<size_t>(count) * sizeof(T);
		}
		return values;
	}

	[[nodiscard]]
	auto overflowed() const -> bool {
		return m_overflowed;
	}

private:
	std::span<const uint8_t> m_data;
	size_t m_offset = 0;
	bool m_overflowed = false;
};

/// @brief Append-only writer producing the layout Reader expects
class Writer {
public:
	template<typename T>
	void write(const T& value) {
		static_assert(std::is_trivially_copyable_v<T>);
		const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
		m_data.insert(m_data.end(), bytes, bytes + sizeof(T));
	}

	void writeString(const std::string& value) {
		write(static_cast<uint32_t>(value.size()));
		m_data.insert(m_data.end(), value.begin(), value.end());
	}

	template<typename T>
	void writeArray(const std::vector<T>& values) {
		write(static_cast<uint32_t>(values.size()));
		if (!values.empty()) {
			const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
			m_data.insert(m_data.end(), bytes, bytes + (values.size() * sizeof(T)));
		}
	}

	auto take() -> std::vector<uint8_t> { return std::move(m_data); }

private:
	std::vector<uint8_t> m_data;
};

constexpr std::array<uint8_t, 4> k_magic {'T', 'A', 'N', 'M'};

}

namespace {

/**
 * @brief Locates the keyframe span containing @p time
 * @return index of the key at or before @p time, and the 0-1 position between it and the next
 *
 * Clamped at both ends, so a caller lands exactly on an endpoint rather than extrapolating off the curve
 */
auto findKeyframe(const std::vector<float>& times, float time) -> std::pair<size_t, float> {
	if (times.size() < 2 || time <= times.front()) {
		return {0, 0.0f};
	}
	if (time >= times.back()) {
		return {times.size() - 1, 0.0f};
	}

	// upper_bound rather than a linear scan: skeletal clips routinely carry hundreds of keys per track and
	// this runs per track per frame
	const auto next = std::upper_bound(times.begin(), times.end(), time);
	const size_t index = static_cast<size_t>(std::distance(times.begin(), next)) - 1;

	const float span = times[index + 1] - times[index];
	const float t = span > 0.0f ? (time - times[index]) / span : 0.0f;
	return {index, t};
}

/// @brief Cubic Hermite as glTF defines it, with tangents scaled by the keyframe delta
template<typename T>
auto cubicSpline(const T& v0, const T& out_tangent0, const T& v1, const T& in_tangent1, float t, float delta) -> T {
	const float t2 = t * t;
	const float t3 = t2 * t;
	return (((2.0f * t3) - (3.0f * t2) + 1.0f) * v0) + (delta * (t3 - (2.0f * t2) + t) * out_tangent0) +
	       (((-2.0f * t3) + (3.0f * t2)) * v1) + (delta * (t3 - t2) * in_tangent1);
}

}

auto AnimationTrack::sampleVec3(float time) const -> glm::vec3 {
	// Scale is the one target whose neutral value isn't zero - posing a node to a zero scale would collapse
	// it, which is far more visibly wrong than leaving it alone
	const glm::vec3 identity = target == TrackTarget::scale ? glm::vec3(1.0f) : glm::vec3(0.0f);
	if (times.empty() || vec3_values.empty()) {
		return identity;
	}

	const auto [index, t] = findKeyframe(times, time);

	if (interpolation == Interpolation::cubic_spline) {
		// Each key is {in-tangent, value, out-tangent}
		const size_t base = index * 3;
		if (base + 1 >= vec3_values.size()) {
			return identity;
		}
		if (t <= 0.0f || base + 4 >= vec3_values.size()) {
			return vec3_values[base + 1];
		}
		const float delta = times[index + 1] - times[index];
		return cubicSpline(vec3_values[base + 1], vec3_values[base + 2], vec3_values[base + 4], vec3_values[base + 3], t, delta);
	}

	if (index >= vec3_values.size()) {
		return identity;
	}
	if (interpolation == Interpolation::step || t <= 0.0f || index + 1 >= vec3_values.size()) {
		return vec3_values[index];
	}
	return glm::mix(vec3_values[index], vec3_values[index + 1], t);
}

auto AnimationTrack::sampleQuat(float time) const -> glm::quat {
	const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
	if (times.empty() || quat_values.empty()) {
		return identity;
	}

	const auto [index, t] = findKeyframe(times, time);

	if (interpolation == Interpolation::cubic_spline) {
		const size_t base = index * 3;
		if (base + 1 >= quat_values.size()) {
			return identity;
		}
		if (t <= 0.0f || base + 4 >= quat_values.size()) {
			return glm::normalize(quat_values[base + 1]);
		}
		const float delta = times[index + 1] - times[index];
		// Hermite on the raw components then renormalise - glTF specifies exactly this for cubic rotations
		return glm::normalize(
		    cubicSpline(quat_values[base + 1], quat_values[base + 2], quat_values[base + 4], quat_values[base + 3], t, delta)
		);
	}

	if (index >= quat_values.size()) {
		return identity;
	}
	if (interpolation == Interpolation::step || t <= 0.0f || index + 1 >= quat_values.size()) {
		return glm::normalize(quat_values[index]);
	}
	// slerp, not mix: linear interpolation of quaternions gives non-uniform angular velocity and a shorter
	// chord through the hypersphere. glm::slerp also takes the shortest arc, which is what glTF wants
	return glm::normalize(glm::slerp(quat_values[index], quat_values[index + 1], t));
}

auto Skin::jointMatrices(std::span<const glm::mat4> joint_world_transforms) const -> std::vector<glm::mat4> {
	ZoneScoped;
	std::vector<glm::mat4> matrices(joints.size(), glm::mat4(1.0f));

	for (size_t i = 0; i < joints.size(); ++i) {
		const glm::mat4 world = i < joint_world_transforms.size() ? joint_world_transforms[i] : glm::mat4(1.0f);
		// An empty inverse-bind array is legal in glTF and means identity for every joint
		const glm::mat4 inverse_bind = i < inverse_bind_matrices.size() ? inverse_bind_matrices[i] : glm::mat4(1.0f);
		matrices[i] = world * inverse_bind;
	}

	return matrices;
}

auto AnimationClip::sample(float time) const -> std::unordered_map<std::string, NodePose> {
	ZoneScoped;
	std::unordered_map<std::string, NodePose> poses;

	for (const auto& track : tracks) {
		if (track.target_node.empty()) {
			continue;
		}
		NodePose& pose = poses[track.target_node];

		switch (track.target) {
			case TrackTarget::translation:
				pose.translation = track.sampleVec3(time);
				pose.has_translation = true;
				break;
			case TrackTarget::rotation:
				pose.rotation = track.sampleQuat(time);
				pose.has_rotation = true;
				break;
			case TrackTarget::scale:
				pose.scale = track.sampleVec3(time);
				pose.has_scale = true;
				break;
		}
	}

	return poses;
}

Animation::Animation(std::span<const uint8_t> data) {
	ZoneScoped;
	Reader reader(data);

	const auto magic = reader.read<std::array<uint8_t, 4>>();
	if (magic != k_magic) {
		TOAST_ERROR("Animation", "Not a .tanim file (bad magic)");
		return;
	}

	const auto version = reader.read<uint32_t>();
	if (version != _detail::animation_format_version) {
		TOAST_ERROR(
		    "Animation", "Unsupported .tanim format version {} (this build reads {})", version, _detail::animation_format_version
		);
		return;
	}

	const auto clip_count = reader.read<uint32_t>();
	m_clips.reserve(clip_count);
	for (uint32_t i = 0; i < clip_count && !reader.overflowed(); ++i) {
		AnimationClip clip;
		clip.name = reader.readString();
		clip.duration = reader.read<float>();

		const auto track_count = reader.read<uint32_t>();
		clip.tracks.reserve(track_count);
		for (uint32_t t = 0; t < track_count && !reader.overflowed(); ++t) {
			AnimationTrack track;
			track.target_node = reader.readString();
			track.target = static_cast<TrackTarget>(reader.read<uint8_t>());
			track.interpolation = static_cast<Interpolation>(reader.read<uint8_t>());
			track.times = reader.readArray<float>();
			track.vec3_values = reader.readArray<glm::vec3>();
			track.quat_values = reader.readArray<glm::quat>();
			clip.tracks.push_back(std::move(track));
		}
		m_clips.push_back(std::move(clip));
	}

	const auto skin_count = reader.read<uint32_t>();
	m_skins.reserve(skin_count);
	for (uint32_t i = 0; i < skin_count && !reader.overflowed(); ++i) {
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
		TOAST_ERROR("Animation", "Truncated .tanim file; loaded {} clip(s) before running out of data", m_clips.size());
	}
}

auto Animation::findClip(std::string_view name) const -> const AnimationClip* {
	for (const auto& clip : m_clips) {
		if (clip.name == name) {
			return &clip;
		}
	}
	return nullptr;
}

auto Animation::toBinary(const std::vector<AnimationClip>& clips, const std::vector<Skin>& skins) -> std::vector<uint8_t> {
	ZoneScoped;
	Writer writer;
	writer.write(k_magic);
	writer.write(_detail::animation_format_version);

	writer.write(static_cast<uint32_t>(clips.size()));
	for (const auto& clip : clips) {
		writer.writeString(clip.name);
		writer.write(clip.duration);

		writer.write(static_cast<uint32_t>(clip.tracks.size()));
		for (const auto& track : clip.tracks) {
			writer.writeString(track.target_node);
			writer.write(static_cast<uint8_t>(track.target));
			writer.write(static_cast<uint8_t>(track.interpolation));
			writer.writeArray(track.times);
			writer.writeArray(track.vec3_values);
			writer.writeArray(track.quat_values);
		}
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
