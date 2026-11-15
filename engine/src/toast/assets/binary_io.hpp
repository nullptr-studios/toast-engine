/**
 * @file binary_io.hpp
 * @author dario
 * @date 13 Nov 2026
 */

#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace assets::_detail {

class Reader {
public:
	explicit Reader(std::span<const uint8_t> data) : m_data(data) { }

	template<typename T>
	auto read() -> T {
		static_assert(std::is_trivially_copyable_v<T>);
		if (m_offset + sizeof(T) > m_data.size()) {
			m_overflowed = true;
			return T {};
		}
		T value {};
		std::memcpy(&value, m_data.data() + m_offset, sizeof(T));
		m_offset += sizeof(T);
		return value;
	}

	auto readString() -> std::string {
		const auto length = read<uint32_t>();
		if (m_overflowed || m_offset + length > m_data.size()) {
			m_overflowed = true;
			return {};
		}
		std::string value(reinterpret_cast<const char*>(m_data.data() + m_offset), length);
		m_offset += length;
		return value;
	}

	template<typename T>
	auto readArray() -> std::vector<T> {
		const auto count = read<uint32_t>();
		if (m_overflowed || m_offset + (static_cast<size_t>(count) * sizeof(T)) > m_data.size()) {
			m_overflowed = true;
			return {};
		}
		std::vector<T> values(count);
		if (count > 0) {
			std::memcpy(values.data(), m_data.data() + m_offset, static_cast<size_t>(count) * sizeof(T));
			m_offset += static_cast<size_t>(count) * sizeof(T);
		}
		return values;
	}

	[[nodiscard]]
	auto overflowed() const -> bool {
		return m_overflowed;
	}

private:
	std::span<const uint8_t> m_data;
	size_t m_offset = 0;
	bool m_overflowed = false;
};

class Writer {
public:
	template<typename T>
	void write(const T& value) {
		static_assert(std::is_trivially_copyable_v<T>);
		const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
		m_data.insert(m_data.end(), bytes, bytes + sizeof(T));
	}

	void writeString(const std::string& value) {
		write(static_cast<uint32_t>(value.size()));
		m_data.insert(m_data.end(), value.begin(), value.end());
	}

	template<typename T>
	void writeArray(const std::vector<T>& values) {
		write(static_cast<uint32_t>(values.size()));
		if (!values.empty()) {
			const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
			m_data.insert(m_data.end(), bytes, bytes + (values.size() * sizeof(T)));
		}
	}

	auto take() -> std::vector<uint8_t> { return std::move(m_data); }

private:
	std::vector<uint8_t> m_data;
};

}
