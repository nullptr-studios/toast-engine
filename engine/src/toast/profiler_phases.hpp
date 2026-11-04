/**
 * @file profiler_phases.hpp
 * @author dario
 * @date 29 Oct 2026
 */

#pragma once

#include <tracy/Tracy.hpp>

#ifdef TRACY_ENABLE
#include <optional>

namespace toast {

/// Closes the last phase zone and opens the next
class PhaseZones {
public:
	PhaseZones() = default;
	PhaseZones(const PhaseZones&) = delete;
	auto operator=(const PhaseZones&) -> PhaseZones& = delete;

	void next(const tracy::SourceLocationData* location) {
		m_zone.reset();
		m_zone.emplace(location);
	}

	void end() { m_zone.reset(); }

private:
	std::optional<tracy::ScopedZone> m_zone;
};

}

#define TOAST_PHASE(phases, label)                                         \
	do {                                                                     \
		static constexpr tracy::SourceLocationData toast_phase_location {      \
		  label, TracyFunction, TracyFile, static_cast<uint32_t>(TracyLine), 0 \
		};                                                                     \
		(phases).next(&toast_phase_location);                                  \
	} while (false)

#else

namespace toast {

class PhaseZones {
public:
	void end() { }
};

}

#define TOAST_PHASE(phases, label) static_cast<void>(phases)

#endif
