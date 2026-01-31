/// @file AudioData.h
/// @author Dario (original), Xein
/// @date 11 Jan 2025
///
/// This comes from Sigma Engine, so it was written for GAM150 "Jackpot Knockout"

#pragma once
#include <glm/vec3.hpp>
#include <string>

namespace audio {

class Data {
public:
	explicit Data(const char* filePath, bool loop = false, bool is3D = false, float reverbAmount = 0.0f, glm::vec3 position = { 0.0f, 0.0f, 0.0f })
			: filePath(filePath), loop(loop), is3D(is3D), reverbAmount(reverbAmount), position(position) {
		uniqueID = filePath;
	}

	~Data() = default;

	[[nodiscard]]
	std::string GetUniqueID() const {
		return uniqueID;
	}

	[[nodiscard]]
	const char* GetFilePath() const {
		return filePath;
	}

	[[nodiscard]]
	float GetVolume() const {
		return volume;
	}

	[[nodiscard]]
	bool IsLoaded() const {
		return loaded;
	}

	[[nodiscard]]
	bool Loop() const {
		return loop;
	}

	[[nodiscard]]
	bool Is3D() const {
		return is3D;
	}

	[[nodiscard]]
	float GetReverbAmount() const {
		return reverbAmount;
	}

	[[nodiscard]]
	glm::vec3 GetPosition() const {
		return position;
	}

	void SetLoaded(bool is_loaded) {
		loaded = is_loaded;
	}

	void SetLengthMS(unsigned int length) {
		lengthMS = length;
	}

	void SetVolume(float new_volume) {
		volume = new_volume;
	}

private:
	std::string uniqueID;
	const char* filePath;
	float volume = 1.0f;
	bool loaded = false;
	bool loop;
	bool is3D;
	unsigned int lengthMS = 0;
	float reverbAmount;
	glm::vec3 position{};
};

}
