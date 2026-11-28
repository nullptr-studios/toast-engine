/**
 * @file value.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief Storage types of parameters and the mapping from C++
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace toast::params {

enum class Type : uint8_t {
	boolean,
	integer,
	floating,
	string,
	vec2,
	vec3,
	vec4,
	quat,
	asset
};

enum class Kind : uint8_t {
	normal,
	advanced,
	debug,
	user
};

enum class Layer : uint8_t {
	project,
	saved,
	runtime
};

inline constexpr size_t k_layer_count = 3;

struct AssetRef {
	std::string uri;

	auto operator==(const AssetRef&) const -> bool = default;
};

using Scalar = std::variant<bool, int64_t, double, std::string, glm::vec2, glm::vec3, glm::vec4, glm::quat, AssetRef>;

using Value = std::variant<Scalar, std::vector<Scalar>>;

template<typename T>
struct Traits;

template<>
struct Traits<bool> {
	static constexpr Type type = Type::boolean;
	static constexpr bool is_array = false;
	using Element = bool;

	static auto toScalar(const bool& v) -> Scalar { return Scalar {v}; }

	static auto fromScalar(const Scalar& s) -> std::optional<bool> {
		if (const auto* v = std::get_if<bool>(&s)) {
			return *v;
		}
		return std::nullopt;
	}
};

template<typename T>
  requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
struct Traits<T> {
	static constexpr Type type = Type::integer;
	static constexpr bool is_array = false;
	using Element = T;

	static auto toScalar(const T& v) -> Scalar { return Scalar {static_cast<int64_t>(v)}; }

	static auto fromScalar(const Scalar& s) -> std::optional<T> {
		if (const auto* v = std::get_if<int64_t>(&s); v != nullptr && std::in_range<T>(*v)) {
			return static_cast<T>(*v);
		}
		return std::nullopt;
	}
};

template<typename T>
  requires std::is_floating_point_v<T>
struct Traits<T> {
	static constexpr Type type = Type::floating;
	static constexpr bool is_array = false;
	using Element = T;

	static auto toScalar(const T& v) -> Scalar { return Scalar {static_cast<double>(v)}; }

	static auto fromScalar(const Scalar& s) -> std::optional<T> {
		if (const auto* d = std::get_if<double>(&s)) {
			return static_cast<T>(*d);
		}
		if (const auto* i = std::get_if<int64_t>(&s)) {
			return static_cast<T>(*i);
		}
		return std::nullopt;
	}
};

namespace detail {

template<typename T, Type kType>
struct DirectTraits {
	static constexpr Type type = kType;
	static constexpr bool is_array = false;
	using Element = T;

	static auto toScalar(const T& v) -> Scalar { return Scalar {v}; }

	static auto fromScalar(const Scalar& s) -> std::optional<T> {
		if (const auto* v = std::get_if<T>(&s)) {
			return *v;
		}
		return std::nullopt;
	}
};

}

template<>
struct Traits<std::string> : detail::DirectTraits<std::string, Type::string> { };

template<>
struct Traits<glm::vec2> : detail::DirectTraits<glm::vec2, Type::vec2> { };

template<>
struct Traits<glm::vec3> : detail::DirectTraits<glm::vec3, Type::vec3> { };

template<>
struct Traits<glm::vec4> : detail::DirectTraits<glm::vec4, Type::vec4> { };

template<>
struct Traits<glm::quat> : detail::DirectTraits<glm::quat, Type::quat> { };

template<>
struct Traits<AssetRef> : detail::DirectTraits<AssetRef, Type::asset> { };

template<typename E>
struct Traits<std::vector<E>> {
	static constexpr Type type = Traits<E>::type;
	static constexpr bool is_array = true;
	using Element = E;

	static auto toScalar(const E& v) -> Scalar { return Traits<E>::toScalar(v); }

	static auto fromScalar(const Scalar& s) -> std::optional<E> { return Traits<E>::fromScalar(s); }
};

template<typename T>
auto toValue(const T& v) -> Value {
	if constexpr (Traits<T>::is_array) {
		std::vector<Scalar> out;
		out.reserve(v.size());
		for (const auto& element : v) {
			out.push_back(Traits<T>::toScalar(element));
		}
		return Value {std::move(out)};
	} else {
		return Value {Traits<T>::toScalar(v)};
	}
}

template<typename T>
auto fromValue(const Value& v) -> std::optional<T> {
	if constexpr (Traits<T>::is_array) {
		const auto* items = std::get_if<std::vector<Scalar>>(&v);
		if (items == nullptr) {
			return std::nullopt;
		}
		T out;
		out.reserve(items->size());
		for (const auto& item : *items) {
			auto element = Traits<T>::fromScalar(item);
			if (!element) {
				return std::nullopt;
			}
			out.push_back(std::move(*element));
		}
		return out;
	} else {
		const auto* scalar = std::get_if<Scalar>(&v);
		return scalar != nullptr ? Traits<T>::fromScalar(*scalar) : std::nullopt;
	}
}

}
