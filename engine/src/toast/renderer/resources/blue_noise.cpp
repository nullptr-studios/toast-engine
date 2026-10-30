#include "toast/renderer/resources/blue_noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <tracy/Tracy.hpp>

namespace renderer::blue_noise {

namespace {

constexpr uint32_t k_texels = k_size * k_size;

/// Ulichney's sigma
constexpr float k_sigma = 1.5f;

/// Kernel window past about five sigma
constexpr int k_reach = 8;

/// Seed points before the swap phase
constexpr uint32_t k_initial_points = k_texels / 10;

/// Toroidal Gaussian per offset
auto energyKernel() -> std::vector<float> {
	std::vector<float> kernel(k_texels);
	for (uint32_t y = 0; y < k_size; ++y) {
		for (uint32_t x = 0; x < k_size; ++x) {
			const auto dx = static_cast<float>(std::min(x, k_size - x));
			const auto dy = static_cast<float>(std::min(y, k_size - y));
			kernel[(y * k_size) + x] = std::exp(-((dx * dx) + (dy * dy)) / (2.0f * k_sigma * k_sigma));
		}
	}
	return kernel;
}

class Field {
public:
	explicit Field(const std::vector<float>& kernel) : m_kernel(kernel), m_energy(k_texels, 0.0f), m_set(k_texels, false) { }

	void toggle(uint32_t index) {
		const float sign = m_set[index] ? -1.0f : 1.0f;
		m_set[index] = !m_set[index];
		const int px = static_cast<int>(index % k_size);
		const int py = static_cast<int>(index / k_size);
		for (int dy = -k_reach; dy <= k_reach; ++dy) {
			const uint32_t y = static_cast<uint32_t>(py + dy + static_cast<int>(k_size)) % k_size;
			const uint32_t ky = static_cast<uint32_t>(dy + static_cast<int>(k_size)) % k_size;
			for (int dx = -k_reach; dx <= k_reach; ++dx) {
				const uint32_t x = static_cast<uint32_t>(px + dx + static_cast<int>(k_size)) % k_size;
				const uint32_t kx = static_cast<uint32_t>(dx + static_cast<int>(k_size)) % k_size;
				m_energy[(y * k_size) + x] += sign * m_kernel[(ky * k_size) + kx];
			}
		}
	}

	[[nodiscard]]
	auto isSet(uint32_t index) const -> bool {
		return m_set[index];
	}

	/// The set point with the most energy around it
	[[nodiscard]]
	auto tightestCluster() const -> uint32_t {
		uint32_t best = 0;
		float best_energy = -1.0f;
		for (uint32_t i = 0; i < k_texels; ++i) {
			if (m_set[i] && m_energy[i] > best_energy) {
				best_energy = m_energy[i];
				best = i;
			}
		}
		return best;
	}

	/// The empty texel with the least energy around it
	[[nodiscard]]
	auto largestVoid() const -> uint32_t {
		uint32_t best = 0;
		float best_energy = 1e30f;
		for (uint32_t i = 0; i < k_texels; ++i) {
			if (!m_set[i] && m_energy[i] < best_energy) {
				best_energy = m_energy[i];
				best = i;
			}
		}
		return best;
	}

private:
	const std::vector<float>& m_kernel;
	std::vector<float> m_energy;
	std::vector<bool> m_set;
};

}

auto generate(uint32_t seed) -> std::vector<uint8_t> {
	ZoneScoped;
	static const std::vector<float> kernel = energyKernel();

	std::mt19937 rng(seed);
	Field initial(kernel);
	for (uint32_t placed = 0; placed < k_initial_points;) {
		const uint32_t index = rng() % k_texels;
		if (!initial.isSet(index)) {
			initial.toggle(index);
			++placed;
		}
	}

	// Move the tightest cluster into the largest void until that stops changing anything
	for (uint32_t pass = 0; pass < k_texels; ++pass) {
		const uint32_t cluster = initial.tightestCluster();
		initial.toggle(cluster);
		const uint32_t hole = initial.largestVoid();
		if (hole == cluster) {
			initial.toggle(cluster);
			break;
		}
		initial.toggle(hole);
	}

	std::vector<uint32_t> rank(k_texels, 0);

	// Ranks below the seed count by peeling clusters off a copy
	Field peel = initial;
	for (uint32_t r = k_initial_points; r-- > 0;) {
		const uint32_t cluster = peel.tightestCluster();
		peel.toggle(cluster);
		rank[cluster] = r;
	}

	// Ranks above it by filling voids
	Field fill = initial;
	for (uint32_t r = k_initial_points; r < k_texels; ++r) {
		const uint32_t hole = fill.largestVoid();
		fill.toggle(hole);
		rank[hole] = r;
	}

	std::vector<uint8_t> out(k_texels);
	for (uint32_t i = 0; i < k_texels; ++i) {
		out[i] = static_cast<uint8_t>((rank[i] * 256u) / k_texels);
	}
	return out;
}

auto generatePacked() -> std::vector<uint32_t> {
	ZoneScoped;
	std::array<std::vector<uint8_t>, 4> channels;
	for (uint32_t c = 0; c < channels.size(); ++c) {
		channels[c] = generate(0x9E3779B9u * (c + 1u));
	}
	std::vector<uint32_t> out(k_texels);
	for (uint32_t i = 0; i < k_texels; ++i) {
		out[i] = uint32_t {channels[0][i]} | (uint32_t {channels[1][i]} << 8u) | (uint32_t {channels[2][i]} << 16u) |
		         (uint32_t {channels[3][i]} << 24u);
	}
	return out;
}

}
