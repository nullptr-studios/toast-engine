/**
 * @file param.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief One parameter
 */

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <toast/params/id.hpp>
#include <toast/params/value.hpp>
#include <type_traits>
#include <vector>

namespace toast::params {

class GroupBase;

template<typename T>
struct Meta {
	std::string_view name;
	std::optional<typename Traits<T>::Element> min;
	std::optional<typename Traits<T>::Element> max;
	std::string_view unit;
	Kind kind = Kind::normal;
	std::string_view description;
	std::span<const std::string_view> options;
	bool requires_restart = false;
	std::string_view asset_type;
};

class TOAST_API ParamBase {
public:
	ParamBase(const ParamBase&) = delete;
	ParamBase(ParamBase&&) = delete;
	auto operator=(const ParamBase&) -> ParamBase& = delete;
	auto operator=(ParamBase&&) -> ParamBase& = delete;
	virtual ~ParamBase();

	[[nodiscard]]
	auto id() const -> std::string_view {
		return m_id;
	}

	[[nodiscard]]
	auto hash() const -> uint64_t {
		return m_hash;
	}

	[[nodiscard]]
	auto name() const -> std::string_view {
		return m_name.empty() ? std::string_view {m_id} : m_name;
	}

	[[nodiscard]]
	auto description() const -> std::string_view {
		return m_description;
	}

	[[nodiscard]]
	auto kind() const -> Kind {
		return m_kind;
	}

	[[nodiscard]]
	auto options() const -> std::span<const std::string_view> {
		return m_options;
	}

	[[nodiscard]]
	auto requiresRestart() const -> bool {
		return m_requires_restart;
	}

	[[nodiscard]]
	auto assetType() const -> std::string_view {
		return m_asset_type;
	}

	[[nodiscard]]
	auto unit() const -> std::string_view {
		return m_unit;
	}

	[[nodiscard]]
	auto group() const -> GroupBase* {
		return m_group;
	}

	[[nodiscard]]
	virtual auto type() const -> Type = 0;

	[[nodiscard]]
	virtual auto isArray() const -> bool = 0;

	[[nodiscard]]
	virtual auto value() const -> Value = 0;

	[[nodiscard]]
	auto snapshot() const -> Value;

	[[nodiscard]]
	virtual auto defaultValue() const -> Value = 0;

	[[nodiscard]]
	virtual auto minValue() const -> std::optional<Scalar> = 0;

	[[nodiscard]]
	virtual auto maxValue() const -> std::optional<Scalar> = 0;

	virtual auto apply(const Value& value) -> bool = 0;

	[[nodiscard]]
	virtual auto accepts(const Value& value) const -> bool = 0;

	auto setLayer(Layer layer, std::optional<Value> value) -> bool;

	[[nodiscard]]
	auto layerValue(Layer layer) const -> const std::optional<Value>& {
		return m_layers[static_cast<size_t>(layer)];
	}

	void request(Value value, Layer layer = Layer::runtime);

	void onChangeValue(std::function<void(const Value&)> callback);

protected:
	struct Info {
		Id id;
		std::string_view name;
		std::string_view description;
		Kind kind = Kind::normal;
		std::span<const std::string_view> options;
		bool requires_restart = false;
		std::string_view asset_type;
		std::string_view unit;
	};

	explicit ParamBase(const Info& info);

	void notify();

	void publish(Value current);

private:
	friend class GroupBase;

	std::string m_id;
	uint64_t m_hash = 0;
	std::string_view m_name;
	std::string_view m_description;
	Kind m_kind = Kind::normal;
	std::span<const std::string_view> m_options;
	bool m_requires_restart = false;
	std::string_view m_asset_type;
	std::string_view m_unit;
	GroupBase* m_group = nullptr;
	std::array<std::optional<Value>, k_layer_count> m_layers;
	std::vector<std::function<void(const Value&)>> m_callbacks;
	mutable std::mutex m_snapshot_mutex;
	Value m_snapshot;

	void resolve();
};

namespace detail {

template<typename E>
concept FloatVector = !std::is_same_v<E, glm::quat> && requires(E v) {
	v.x;
	v[0];
	E::length();
	requires std::is_floating_point_v<typename E::value_type>;
};

template<typename E>
void clampElement(E& value, const std::optional<E>& min, const std::optional<E>& max) {
	if constexpr (std::is_arithmetic_v<E> && !std::is_same_v<E, bool>) {
		if (min && value < *min) {
			value = *min;
		}
		if (max && value > *max) {
			value = *max;
		}
	} else if constexpr (FloatVector<E>) {
		for (int i = 0; i < E::length(); ++i) {
			if (min && value[i] < (*min)[i]) {
				value[i] = (*min)[i];
			}
			if (max && value[i] > (*max)[i]) {
				value[i] = (*max)[i];
			}
		}
	}
}

template<typename T>
auto parseValue(const Value& value) -> std::optional<T> {
	using Element = typename Traits<T>::Element;
	if constexpr (std::is_integral_v<Element> && !std::is_same_v<Element, bool>) {
		const int64_t lowest = static_cast<int64_t>(std::numeric_limits<Element>::min());
		const int64_t highest = std::in_range<int64_t>(std::numeric_limits<Element>::max())
		                            ? static_cast<int64_t>(std::numeric_limits<Element>::max())
		                            : std::numeric_limits<int64_t>::max();
		const auto saturate = [lowest, highest](Scalar& scalar) {
			if (auto* integer = std::get_if<int64_t>(&scalar)) {
				*integer = std::clamp(*integer, lowest, highest);
			}
		};
		Value saturated = value;
		if (auto* scalar = std::get_if<Scalar>(&saturated)) {
			saturate(*scalar);
		} else if (auto* items = std::get_if<std::vector<Scalar>>(&saturated)) {
			std::ranges::for_each(*items, saturate);
		}
		return fromValue<T>(saturated);
	} else {
		return fromValue<T>(value);
	}
}

template<typename E>
auto isFinite(const E& value) -> bool {
	if constexpr (std::is_floating_point_v<E>) {
		return std::isfinite(value);
	} else if constexpr (requires {
		                     value.x;
		                     value[0];
	                     }) {
		for (int i = 0; i < E::length(); ++i) {
			if (!std::isfinite(value[i])) {
				return false;
			}
		}
		return true;
	} else {
		return true;
	}
}

template<typename T>
auto isFiniteValue(const T& value) -> bool {
	if constexpr (Traits<T>::is_array) {
		return std::ranges::all_of(value, [](const auto& element) { return isFinite(element); });
	} else {
		return isFinite(value);
	}
}

}

template<typename T>
class Param final : public ParamBase {
public:
	using Element = typename Traits<T>::Element;

	Param(Id id, T default_value, Meta<T> meta = {})
	    : ParamBase(
	          Info {id, meta.name, meta.description, meta.kind, meta.options, meta.requires_restart, meta.asset_type, meta.unit}
	      ),
	      m_value(default_value),
	      m_default(std::move(default_value)),
	      m_meta(meta) {
		publish(toValue(m_value));
	}

	[[nodiscard]]
	auto get() const -> const T& {
		return m_value;
	}

	operator const T&() const {    // NOLINT(google-explicit-constructor) - the whole point is reading it like a T
		return m_value;
	}

	void onChange(std::function<void(const T&)> callback) {
		callback(m_value);
		onChangeValue([callback = std::move(callback)](const Value& v) {
			if (auto typed = fromValue<T>(v)) {
				callback(*typed);
			}
		});
	}

	[[nodiscard]]
	auto type() const -> Type override {
		return Traits<T>::type;
	}

	[[nodiscard]]
	auto isArray() const -> bool override {
		return Traits<T>::is_array;
	}

	[[nodiscard]]
	auto value() const -> Value override {
		return toValue(m_value);
	}

	[[nodiscard]]
	auto defaultValue() const -> Value override {
		return toValue(m_default);
	}

	[[nodiscard]]
	auto minValue() const -> std::optional<Scalar> override {
		return m_meta.min ? std::optional<Scalar> {Traits<T>::toScalar(*m_meta.min)} : std::nullopt;
	}

	[[nodiscard]]
	auto maxValue() const -> std::optional<Scalar> override {
		return m_meta.max ? std::optional<Scalar> {Traits<T>::toScalar(*m_meta.max)} : std::nullopt;
	}

	[[nodiscard]]
	auto accepts(const Value& value) const -> bool override {
		const auto parsed = detail::parseValue<T>(value);
		return parsed && detail::isFiniteValue(*parsed);
	}

	void request(const T& value) { ParamBase::request(toValue(value)); }

	using ParamBase::request;

	auto apply(const Value& value) -> bool override {
		auto parsed = detail::parseValue<T>(value);
		if (!parsed || !detail::isFiniteValue(*parsed)) {
			return false;
		}
		if constexpr (Traits<T>::is_array) {
			for (auto& element : *parsed) {
				detail::clampElement(element, m_meta.min, m_meta.max);
			}
		} else {
			detail::clampElement(*parsed, m_meta.min, m_meta.max);
		}
		if (*parsed == m_value) {
			return true;
		}
		m_value = std::move(*parsed);
		publish(toValue(m_value));
		notify();
		return true;
	}

private:
	T m_value;
	T m_default;
	Meta<T> m_meta;
};

}
