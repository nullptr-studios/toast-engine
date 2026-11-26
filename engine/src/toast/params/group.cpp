#include "group.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <toast/log.hpp>

namespace toast::params {
namespace {

struct PrefixRange {
	uintptr_t start;
	size_t size;
	std::string prefix;
};

struct Directory {
	std::mutex mutex;
	std::map<uintptr_t, GroupBase*> groups;
	std::vector<GroupBase*> order;
	std::vector<PrefixRange> prefixes;
};

auto directory() -> Directory& {
	static Directory instance;
	return instance;
}

}

namespace detail {

void addPrefix(const void* start, size_t size, std::string_view prefix) {
	auto& dir = directory();
	const std::lock_guard lock(dir.mutex);
	dir.prefixes.push_back({reinterpret_cast<uintptr_t>(start), size, std::string {prefix}});
}

void removePrefix(const void* start) {
	auto& dir = directory();
	const std::lock_guard lock(dir.mutex);
	std::erase_if(dir.prefixes, [start](const PrefixRange& range) { return range.start == reinterpret_cast<uintptr_t>(start); });
}

auto locateGroup(const void* address) -> GroupBase* {
	auto& dir = directory();
	const std::lock_guard lock(dir.mutex);
	const auto at = reinterpret_cast<uintptr_t>(address);
	auto it = dir.groups.upper_bound(at);
	if (it == dir.groups.begin()) {
		return nullptr;
	}
	return std::prev(it)->second;
}

auto prefixFor(const void* address) -> std::string {
	auto& dir = directory();
	const std::lock_guard lock(dir.mutex);
	const auto at = reinterpret_cast<uintptr_t>(address);

	std::vector<const PrefixRange*> containing;
	for (const auto& range : dir.prefixes) {
		if (at >= range.start && at < range.start + range.size) {
			containing.push_back(&range);
		}
	}
	std::ranges::sort(containing, [](const PrefixRange* a, const PrefixRange* b) { return a->size > b->size; });

	std::string result;
	for (const auto* range : containing) {
		if (!result.empty()) {
			result += '.';
		}
		result += range->prefix;
	}
	return result;
}

auto liveGroups() -> std::vector<GroupBase*> {
	auto& dir = directory();
	const std::lock_guard lock(dir.mutex);
	return dir.order;
}

}

GroupBase::GroupBase(std::string_view name) : m_name(name) {
	auto& dir = directory();
	const std::lock_guard lock(dir.mutex);
	for (const GroupBase* other : dir.order) {
		if (other->name() == name) {
			TOAST_WARN("Params", "A second group named '{}' is live; lookups by name find the first", name);
			break;
		}
	}
	dir.groups.emplace(reinterpret_cast<uintptr_t>(this), this);
	dir.order.push_back(this);
}

GroupBase::~GroupBase() {
	auto& dir = directory();
	const std::lock_guard lock(dir.mutex);
	dir.groups.erase(reinterpret_cast<uintptr_t>(this));
	std::erase(dir.order, this);
}

auto GroupBase::find(std::string_view id) const -> ParamBase* {
	const auto it = m_by_hash.find(fnv1a(id));
	if (it == m_by_hash.end() || it->second->id() != id) {
		return nullptr;
	}
	return it->second;
}

auto GroupBase::operator[](std::string_view id) const -> ParamBase& {
	ParamBase* param = find(id);
	if (param == nullptr) {
		throw std::out_of_range(std::string {"no parameter '"} + std::string {id} + "' in group '" + std::string {m_name} + "'");
	}
	return *param;
}

auto GroupBase::add(ParamBase& param) -> bool {
	if (m_by_hash.contains(param.hash())) {
		TOAST_ERROR("Params", "Duplicate parameter id '{}' in group '{}'; the first declaration is kept", param.id(), m_name);
		return false;
	}
	m_by_hash.emplace(param.hash(), &param);
	m_params.push_back(&param);
	return true;
}

void GroupBase::remove(ParamBase& param) {
	if (const auto it = m_by_hash.find(param.hash()); it != m_by_hash.end() && it->second == &param) {
		m_by_hash.erase(it);
		std::erase(m_params, &param);
	}
}

}
