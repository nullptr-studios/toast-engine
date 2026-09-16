/**
 * @file mass_accumulator.hpp
 * @author dario
 * @date 08/09/2026
 */

#pragma once
#include "voxel_constants.hpp"

#include <cassert>
#include <cstdint>
#include <glm/glm.hpp>

namespace toast::voxel {

inline constexpr int64_t k_moment_safe_limit = int64_t {1} << 62;

struct MassMoments {
	/// Sum of densities
	int64_t mass = 0;

	/// Sum of density * coordinate
	int64_t m_x = 0;
	int64_t m_y = 0;
	int64_t m_z = 0;

	/// Sum of density * coordinate*coordinate
	int64_t m_xx = 0;
	int64_t m_yy = 0;
	int64_t m_zz = 0;

	/// Sum of density * coordinate_a * coordinate_b
	int64_t m_xy = 0;
	int64_t m_xz = 0;
	int64_t m_yz = 0;

	[[nodiscard]]
	constexpr auto operator==(const MassMoments&) const noexcept -> bool = default;

	[[nodiscard]]
	constexpr auto isEmpty() const noexcept -> bool {
		return mass == 0;
	}

	constexpr void add(int32_t x, int32_t y, int32_t z, uint32_t density) noexcept {
		accumulate(x, y, z, static_cast<int64_t>(density));
	}

	constexpr void remove(int32_t x, int32_t y, int32_t z, uint32_t density) noexcept {
		accumulate(x, y, z, -static_cast<int64_t>(density));
		assert(mass >= 0);
	}

	constexpr auto operator+=(const MassMoments& other) noexcept -> MassMoments& {
		mass += other.mass;
		m_x += other.m_x;
		m_y += other.m_y;
		m_z += other.m_z;
		m_xx += other.m_xx;
		m_yy += other.m_yy;
		m_zz += other.m_zz;
		m_xy += other.m_xy;
		m_xz += other.m_xz;
		m_yz += other.m_yz;
		checkHeadroom();
		return *this;
	}

	[[nodiscard]]
	constexpr auto operator+(const MassMoments& other) const noexcept -> MassMoments {
		MassMoments out = *this;
		out += other;
		return out;
	}

	[[nodiscard]]
	constexpr auto shifted(int32_t ox, int32_t oy, int32_t oz) const noexcept -> MassMoments {
		const int64_t dx = ox;
		const int64_t dy = oy;
		const int64_t dz = oz;

		MassMoments out;
		out.mass = mass;
		out.m_x = m_x + dx * mass;
		out.m_y = m_y + dy * mass;
		out.m_z = m_z + dz * mass;
		out.m_xx = m_xx + 2 * dx * m_x + dx * dx * mass;
		out.m_yy = m_yy + 2 * dy * m_y + dy * dy * mass;
		out.m_zz = m_zz + 2 * dz * m_z + dz * dz * mass;
		out.m_xy = m_xy + dx * m_y + dy * m_x + dx * dy * mass;
		out.m_xz = m_xz + dx * m_z + dz * m_x + dx * dz * mass;
		out.m_yz = m_yz + dy * m_z + dz * m_y + dy * dz * mass;
		out.checkHeadroom();
		return out;
	}

private:
	constexpr void accumulate(int32_t x, int32_t y, int32_t z, int64_t signed_density) noexcept {
		const int64_t lx = x;
		const int64_t ly = y;
		const int64_t lz = z;

		mass += signed_density;
		m_x += signed_density * lx;
		m_y += signed_density * ly;
		m_z += signed_density * lz;
		m_xx += signed_density * lx * lx;
		m_yy += signed_density * ly * ly;
		m_zz += signed_density * lz * lz;
		m_xy += signed_density * lx * ly;
		m_xz += signed_density * lx * lz;
		m_yz += signed_density * ly * lz;
		checkHeadroom();
	}

	constexpr void checkHeadroom() const noexcept {
		assert(m_xx < k_moment_safe_limit && m_yy < k_moment_safe_limit && m_zz < k_moment_safe_limit);
	}
};

struct MassProperties {
	/// kg
	float mass = 0.0f;

	/// Volume local metres
	glm::vec3 center_of_mass {0.0f};

	/// kg·m² about the centre of mass
	glm::mat3 inertia {0.0f};
};

[[nodiscard]]
inline auto resolve(const MassMoments& moments, float voxel_size = k_voxel_size) -> MassProperties {
	MassProperties out;
	if (moments.mass <= 0) {
		return out;
	}

	const double s = voxel_size;
	const double s3 = s * s * s;
	const double s5 = s3 * s * s;

	const double density_sum = static_cast<double>(moments.mass);
	const double total_mass = density_sum * s3;

	// sum(d * (l_a + 1/2) * (l_b + 1/2)) = m_ab + (m_a + m_b)/2 + density_sum/4
	const auto product = [&](int64_t m_ab, int64_t m_a, int64_t m_b) {
		return static_cast<double>(m_ab) + 0.5 * (static_cast<double>(m_a) + static_cast<double>(m_b)) + 0.25 * density_sum;
	};

	const double p_xx = product(moments.m_xx, moments.m_x, moments.m_x);
	const double p_yy = product(moments.m_yy, moments.m_y, moments.m_y);
	const double p_zz = product(moments.m_zz, moments.m_z, moments.m_z);
	const double p_xy = product(moments.m_xy, moments.m_x, moments.m_y);
	const double p_xz = product(moments.m_xz, moments.m_x, moments.m_z);
	const double p_yz = product(moments.m_yz, moments.m_y, moments.m_z);

	const double com_x = (static_cast<double>(moments.m_x) / density_sum + 0.5) * s;
	const double com_y = (static_cast<double>(moments.m_y) / density_sum + 0.5) * s;
	const double com_z = (static_cast<double>(moments.m_z) / density_sum + 0.5) * s;

	// A cube of edge s adds m * s² / 6 to every diagonal
	const double self_term = total_mass * s * s / 6.0;

	double i_xx = s5 * (p_yy + p_zz) + self_term;
	double i_yy = s5 * (p_xx + p_zz) + self_term;
	double i_zz = s5 * (p_xx + p_yy) + self_term;
	double i_xy = -s5 * p_xy;
	double i_xz = -s5 * p_xz;
	double i_yz = -s5 * p_yz;

	i_xx -= total_mass * (com_y * com_y + com_z * com_z);
	i_yy -= total_mass * (com_x * com_x + com_z * com_z);
	i_zz -= total_mass * (com_x * com_x + com_y * com_y);
	i_xy += total_mass * com_x * com_y;
	i_xz += total_mass * com_x * com_z;
	i_yz += total_mass * com_y * com_z;

	out.mass = static_cast<float>(total_mass);
	out.center_of_mass = glm::vec3(static_cast<float>(com_x), static_cast<float>(com_y), static_cast<float>(com_z));
	out.inertia = glm::mat3(
	    static_cast<float>(i_xx),
	    static_cast<float>(i_xy),
	    static_cast<float>(i_xz),
	    static_cast<float>(i_xy),
	    static_cast<float>(i_yy),
	    static_cast<float>(i_yz),
	    static_cast<float>(i_xz),
	    static_cast<float>(i_yz),
	    static_cast<float>(i_zz)
	);
	return out;
}

}
