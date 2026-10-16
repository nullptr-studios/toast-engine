#include "profiler_sections.hpp"

#include <tracy/Tracy.hpp>

namespace toast {

namespace {
[[maybe_unused]]
auto label(ProfilerSections::Mode mode) -> const char* {
	switch (mode) {
		case ProfilerSections::Mode::editor: return "Editor";
		case ProfilerSections::Mode::play: return "Play";
		case ProfilerSections::Mode::simulate: return "Simulate";
		case ProfilerSections::Mode::none: break;
	}
	return "";
}

auto hasWorld(ProfilerSections::Mode mode) -> bool {
	return mode == ProfilerSections::Mode::play || mode == ProfilerSections::Mode::simulate;
}
}

ProfilerSections::~ProfilerSections() {
	leaveAll();
}

void ProfilerSections::leaveAll() {
	TracySectionLeave(m_world_id);
	TracySectionLeave(m_mode_id);
	m_world_id = 0;
	m_mode_id = 0;
}

void ProfilerSections::update(Mode mode, std::string_view world) {
	if (!hasWorld(mode)) {
		world = {};
	}

	const bool connected = TracyIsConnected;
	if (!connected) {
		m_world_id = 0;
		m_mode_id = 0;
	}

	if (mode != m_mode) {
		leaveAll();
	} else if (world != m_world) {
		TracySectionLeave(m_world_id);
		m_world_id = 0;
	}
	m_mode = mode;
	m_world = world;

	if (!connected || m_mode == Mode::none) {
		return;
	}

	if (m_mode_id == 0) {
		m_mode_id = TracySectionEnter("%s", label(m_mode));
	}
	if (hasWorld(m_mode) && m_world_id == 0) {
		m_world_id = TracySectionEnter("%s", m_world.c_str());
	}
}
}
