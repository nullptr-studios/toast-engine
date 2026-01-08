/**
 * @file Time.hpp
 * @author Xein
 * @date 15/05/25
 *
 * @brief Controls the time of the application
 */

#pragma once
#include <chrono>

/// @brief Class that controls the time of the application
class Time {
	using clock_t = std::chrono::high_resolution_clock;
	using time_point_t = clock_t::time_point;

	static constexpr float MAX_DELTA = 1.0f / 15.0f;
	static constexpr float MAX_FIXED = 1.0f / 25.0f;

public:
	Time();

	/// @brief updates the clocks
	void Tick() noexcept;

	/// @brief updates the physics clocks
	void PhysTick() noexcept;

	/// @brief Returns the time the last frame took to process
	static double delta() noexcept;

	/// @brief Returns delta without scaling
	static double raw_delta() noexcept;

	/// @brief Returns fixed delta
	static double fixed_delta() noexcept;

	/// @brief Returns non-cached fixed delta
	double fixed_delta_t() noexcept;

	/// @brief Returns fixed delta without scaling
	static double raw_fixed_delta() noexcept;

	/// @brief Returns the time the application has been running
	static double uptime() noexcept;

	/// @brief Returns the system clock
	static double system() noexcept;

	/// @brief Returns Time class instance
	static Time* GetInstance() noexcept;

	/// @brief Returns value of multiplier
	static float scale();

	/// @brief Modifies value of multiplier
	static void scale(float value);

private:
	/// @brief Timestamp for this frame
	clock_t::time_point m_now;

	///@brief Timestamp for this Physics frame
	clock_t::time_point m_nowPhys;

	/// @brief Timestamp for last frame
	clock_t::time_point m_previous;

	///@brief Timestamp for the last Physics frame
	clock_t::time_point m_previousPhys;

	/// @brief Timestamp for application start
	clock_t::time_point m_startTime;

	/// @brief Time class instance
	static Time* m_instance;

	/// @brief Raw Delta variable
	float m_deltaRaw = 0.0f;

	/// @brief Delta variable
	float m_delta = 0.0f;

	/// @brief Raw Fixed Delta variable
	float m_deltaFixedRaw = 0.0f;

	/// @brief Fixed Delta variable;
	float m_deltaFixed = 0.0f;

	/// @brief Delta time multiplier
	float m_deltaScale = 1.0f;
};
