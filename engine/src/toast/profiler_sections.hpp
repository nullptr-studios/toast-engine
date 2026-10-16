/**
 * @file profiler_sections.hpp
 * @author Xein
 * @date 16 Oct 2026
 * @brief Tracy sections marking whether the engine is in the editor, playing or simulating
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace toast {

class ProfilerSections {
public:
	enum class Mode : uint8_t {
		none,
		editor,
		play,
		simulate,
	};

	ProfilerSections() = default;
	ProfilerSections(const ProfilerSections&) = delete;
	auto operator=(const ProfilerSections&) -> ProfilerSections& = delete;
	~ProfilerSections();

	void update(Mode mode, std::string_view world);

private:
	Mode m_mode = Mode::none;
	std::string m_world;
	uint32_t m_mode_id = 0;
	uint32_t m_world_id = 0;

	void leaveAll();
};
}
