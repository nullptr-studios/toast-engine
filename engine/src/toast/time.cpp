#include "time.hpp"

#include <algorithm>
#include <chrono>
#include <toast/log.hpp>

Time::Time() {
	ZoneScoped;
	TOAST_INFO("Time", "Initializing Time");
	TOAST_ASSERT(not instance, "Time", "Time class can only be created once");
	instance = this;

	auto now = clock_t::now();
	m_now = now;
	m_previous = now;
	m_start_time = now;
	m_now_render = now;
	m_previous_render = now;
}

Time::~Time() {
	instance = nullptr;
}

auto Time::get() noexcept -> Time& {
	TOAST_ASSERT(instance, "Time", "Tried to get Time but it doesn't exist yet");
	return *instance;
}

void Time::tick() noexcept {
	m_previous = m_now;
	m_now = clock_t::now();

	std::chrono::duration<double> t = m_now - m_previous;

	const double raw = t.count();
	const bool is_paused = m_paused.load(std::memory_order_relaxed);

	// goofy ahh loc
	const double dt =
	    is_paused ? 0.0
	              : std::min(raw * m_delta_scale.load(std::memory_order_relaxed), m_max_delta.load(std::memory_order_relaxed));

	// sequence lock write -x
	// odd means write in progress
	// even means stable
	m_sim_seq.fetch_add(1, std::memory_order_release);
	m_pub_delta.store(dt, std::memory_order_relaxed);
	m_pub_raw_delta.store(raw, std::memory_order_relaxed);
	m_pub_frame.store(++m_sim_frame, std::memory_order_relaxed);
	m_pub_paused.store(is_paused, std::memory_order_relaxed);
	m_sim_seq.fetch_add(1, std::memory_order_release);
}

void Time::renderTick() noexcept {
	m_previous_render = m_now_render;
	m_now_render = clock_t::now();

	std::chrono::duration<double> t = m_now_render - m_previous_render;
	const double render_dt = t.count();

	m_render_seq.fetch_add(1, std::memory_order_release);
	m_pub_render_delta.store(render_dt, std::memory_order_relaxed);
	m_pub_render_frame.store(++m_render_count, std::memory_order_relaxed);
	m_render_seq.fetch_add(1, std::memory_order_release);
}

auto Time::delta() noexcept -> double {
	return instance->m_pub_delta.load(std::memory_order_acquire);
}

auto Time::rawDelta() noexcept -> double {
	return instance->m_pub_raw_delta.load(std::memory_order_acquire);
}

auto Time::renderDelta() noexcept -> double {
	return instance->m_pub_render_delta.load(std::memory_order_acquire);
}

auto Time::frame() noexcept -> uint64_t {
	return instance->m_pub_frame.load(std::memory_order_acquire);
}

auto Time::renderFrame() noexcept -> uint64_t {
	return instance->m_pub_render_frame.load(std::memory_order_acquire);
}

auto Time::snapshot() noexcept -> TimeSnapshot {
	TimeSnapshot out;
	uint32_t s1, s2;

	// Read main thread fields consistently
	do {
		s1 = instance->m_sim_seq.load(std::memory_order_acquire);
		if (s1 & 1) [[unlikely]] {
			continue;
		}
		out.delta = instance->m_pub_delta.load(std::memory_order_relaxed);
		out.raw_delta = instance->m_pub_raw_delta.load(std::memory_order_relaxed);
		out.frame = instance->m_pub_frame.load(std::memory_order_relaxed);
		out.paused = instance->m_pub_paused.load(std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_acquire);
		s2 = instance->m_sim_seq.load(std::memory_order_relaxed);
	} while (s1 != s2);

	// Read render fields consistently
	do {
		s1 = instance->m_render_seq.load(std::memory_order_acquire);
		if (s1 & 1) [[unlikely]] {
			continue;
		}
		out.render_delta = instance->m_pub_render_delta.load(std::memory_order_relaxed);
		out.render_frame = instance->m_pub_render_frame.load(std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_acquire);
		s2 = instance->m_render_seq.load(std::memory_order_relaxed);
	} while (s1 != s2);

	out.uptime = uptime();
	return out;
}

void Time::pause() noexcept {
	instance->m_paused.store(true, std::memory_order_relaxed);
}

void Time::resume() noexcept {
	instance->m_paused.store(false, std::memory_order_relaxed);
}

auto Time::paused() noexcept -> bool {
	return instance->m_paused.load(std::memory_order_relaxed);
}

auto Time::tps() noexcept -> double {
	const double d = instance->m_pub_delta.load(std::memory_order_acquire);
	return d > 0.0 ? 1.0 / d : 0.0;
}

auto Time::fps() noexcept -> double {
	const double d = instance->m_pub_render_delta.load(std::memory_order_acquire);
	return d > 0.0 ? 1.0 / d : 0.0;
}

auto Time::scale() noexcept -> double {
	return instance->m_delta_scale.load(std::memory_order_relaxed);
}

void Time::scale(double value) {
	instance->m_delta_scale.store(value, std::memory_order_relaxed);
}

auto Time::uptime() noexcept -> double {
	std::chrono::duration<double> t = clock_t::now() - instance->m_start_time;
	return t.count();
}

auto Time::system() noexcept -> double {
	std::chrono::duration<double> t = std::chrono::system_clock::now().time_since_epoch();
	return t.count();
}

void Time::setMaxDelta(double delta) {
	instance->m_max_delta.store(delta, std::memory_order_relaxed);
}
