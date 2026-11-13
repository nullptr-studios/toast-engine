#include "animation.hpp"

#include "binary_io.hpp"

namespace assets {

namespace {

using _detail::Reader;
using _detail::Writer;

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

auto AnimationClip::sample(float time) const -> std::unordered_map<std::string, NodePose> {
	ZoneScoped;
	std::unordered_map<std::string, NodePose> poses;

	for (const auto& track : tracks) {
		if (track.target_node.empty()) {
			continue;
		}
		NodePose& pose = poses[track.target_node];

		switch (track.target) {
			case TrackTarget::translation: pose.translation = track.sampleVec3(time); break;
			case TrackTarget::rotation: pose.rotation = track.sampleQuat(time); break;
			case TrackTarget::scale:
				// pose.scale = track.sampleVec3(time);
				// pose.has_scale = true; // fuck the scale
				break;
		}
	}

	return poses;
}

void AnimationClip::sampleInto(float time, std::span<const int32_t> track_to_bone, std::span<BonePose> pose) const {
	ZoneScoped;
	const size_t count = std::min(tracks.size(), track_to_bone.size());
	for (size_t i = 0; i < count; ++i) {
		const int32_t bone = track_to_bone[i];
		if (bone < 0 || std::cmp_greater_equal(bone, pose.size())) {
			continue;
		}
		const auto& track = tracks[i];
		switch (track.target) {
			case TrackTarget::translation: pose[bone].translation = track.sampleVec3(time); break;
			case TrackTarget::rotation: pose[bone].rotation = track.sampleQuat(time); break;
			case TrackTarget::scale: break;    // ignored like sample()
		}
	}
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

	m_clip.name = reader.readString();
	m_clip.duration = reader.read<float>();
	const auto track_count = reader.read<uint32_t>();
	m_clip.tracks.reserve(track_count);
	for (uint32_t t = 0; t < track_count && !reader.overflowed(); ++t) {
		AnimationTrack track;
		track.target_node = reader.readString();
		track.target = static_cast<TrackTarget>(reader.read<uint8_t>());
		track.interpolation = static_cast<Interpolation>(reader.read<uint8_t>());
		track.times = reader.readArray<float>();
		track.vec3_values = reader.readArray<glm::vec3>();
		track.quat_values = reader.readArray<glm::quat>();
		m_clip.tracks.push_back(std::move(track));
	}

	if (reader.overflowed()) {
		TOAST_ERROR("Animation", "Truncated .tanim file '{}'", m_clip.name);
	}
}

auto Animation::toBinary(const AnimationClip& clip) -> std::vector<uint8_t> {
	ZoneScoped;
	Writer writer;
	writer.write(k_magic);
	writer.write(_detail::animation_format_version);
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
	return writer.take();
}

}
