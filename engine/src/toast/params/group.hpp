/**
 * @file group.hpp
 * @author Xein
 * @date 24 Nov 2026
 *
 * @brief A named set of parameters declared as the members
 */

#pragma once
#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <toast/export.hpp>
#include <toast/params/param.hpp>
#include <unordered_map>
#include <vector>

namespace toast::params {

template<size_t N>
struct FixedString {
	char value[N];                                       // NOLINT(modernize-avoid-c-arrays)

	constexpr FixedString(const char (&literal)[N]) {    // NOLINT(google-explicit-constructor, modernize-avoid-c-arrays)
		std::copy_n(literal, N, value);
	}

	constexpr operator std::string_view() const {    // NOLINT(google-explicit-constructor)
		return {value, N - 1};
	}
};

namespace detail {

TOAST_API void addPrefix(const void* start, size_t size, std::string_view prefix);
TOAST_API void removePrefix(const void* start);

auto locateGroup(const void* address) -> GroupBase*;

auto prefixFor(const void* address) -> std::string;

auto liveGroups() -> std::vector<GroupBase*>;

}

class TOAST_API GroupBase {
public:
	GroupBase(const GroupBase&) = delete;
	GroupBase(GroupBase&&) = delete;
	auto operator=(const GroupBase&) -> GroupBase& = delete;
	auto operator=(GroupBase&&) -> GroupBase& = delete;

	[[nodiscard]]
	auto name() const -> std::string_view {
		return m_name;
	}

	[[nodiscard]]
	auto params() const -> std::span<ParamBase* const> {
		return m_params;
	}

	[[nodiscard]]
	auto find(std::string_view id) const -> ParamBase*;

	auto operator[](std::string_view id) const -> ParamBase&;

	[[nodiscard]]
	auto dirty(Layer layer) const -> bool {
		return (m_dirty & (1U << static_cast<unsigned>(layer))) != 0;
	}

	void markDirty(Layer layer) { m_dirty = static_cast<uint8_t>(m_dirty | (1U << static_cast<unsigned>(layer))); }

	void clearDirty(Layer layer) { m_dirty = static_cast<uint8_t>(m_dirty & ~(1U << static_cast<unsigned>(layer))); }

protected:
	explicit GroupBase(std::string_view name);
	~GroupBase();

private:
	friend class ParamBase;

	auto add(ParamBase& param) -> bool;
	void remove(ParamBase& param);

	std::string_view m_name;
	uint8_t m_dirty = 0;
	std::vector<ParamBase*> m_params;
	std::unordered_map<uint64_t, ParamBase*> m_by_hash;
};

template<FixedString Name>
class Group : public GroupBase {
protected:
	Group() : GroupBase(std::string_view {Name}) { }

	~Group() = default;
};

template<FixedString Prefix, typename Derived>
class Sub {
protected:
	Sub() { detail::addPrefix(this, sizeof(Derived), std::string_view {Prefix}); }

	~Sub() { detail::removePrefix(this); }

public:
	Sub(const Sub&) = delete;
	Sub(Sub&&) = delete;
	auto operator=(const Sub&) -> Sub& = delete;
	auto operator=(Sub&&) -> Sub& = delete;
};

}
