#include <Engine/Core/Log.hpp>
#include <Engine/Core/Time.hpp>
#include <chrono>

Time* Time::m_instance = nullptr;

Time::Time() {
	TOAST_INFO("Initializing Time");

	if (m_instance) {
		throw ToastException("Trying to create Time class but it already exists");
	}
	m_instance = this;

	m_now = clock_t::now();
	m_previous = clock_t::now();
	m_startTime = clock_t::now();
	m_nowPhys = clock_t::now();
	m_previousPhys = clock_t::now();

	// m_deltaScale = 1;
}

double Time::delta() noexcept {
	return m_instance->m_delta;
}

double Time::raw_delta() noexcept {
	return m_instance->m_deltaRaw;
}

double Time::fixed_delta() noexcept {
	return m_instance->m_deltaFixed;
}

double Time::fixed_delta_t() noexcept {
	auto now = clock_t::now();
	std::chrono::duration<double> t = now - m_nowPhys;
	return t.count();
}

double Time::raw_fixed_delta() noexcept {
	return m_instance->m_deltaFixedRaw;
}

float Time::scale() {
	return m_instance->m_deltaScale;
}

void Time::scale(float value) {
	m_instance->m_deltaScale = value;
}

Time* Time::GetInstance() noexcept {
	return m_instance;
}

void Time::Tick() noexcept {
	m_previous = m_now;
	m_now = clock_t::now();
	std::chrono::duration<double> t = m_now - m_previous;
	m_deltaRaw = t.count();
	m_delta = std::min(m_deltaRaw / m_deltaScale, MAX_DELTA);
}

void Time::PhysTick() noexcept {
	m_previousPhys = m_nowPhys;
	m_nowPhys = clock_t::now();
	std::chrono::duration<double> t = m_nowPhys - m_previousPhys;
	m_deltaFixedRaw = t.count();
	m_deltaFixed = std::min(m_deltaFixedRaw / m_deltaScale, MAX_FIXED);
}

double Time::uptime() noexcept {
	std::chrono::duration<double> t = m_instance->m_now - m_instance->m_startTime;
	return t.count();
}

double Time::system() noexcept {
	auto now = std::chrono::system_clock::now();
	std::chrono::duration<double> t = now.time_since_epoch();
	return t.count();
}
