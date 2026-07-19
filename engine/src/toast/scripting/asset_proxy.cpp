#include "asset_proxy.hpp"

#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>

namespace scripting {

AssetProxy::AssetProxy(toast::UID uid) : m_handle(assets::load(uid)) { }

AssetProxy::AssetProxy(assets::HandleBase handle) : m_handle(std::move(handle)) { }

auto AssetProxy::path() const -> std::string {
	return std::string(m_handle.path());
}

auto AssetProxy::uid() const -> toast::UID {
	return m_handle.uid();
}

auto AssetProxy::hasValue() const -> bool {
	return m_handle.hasValue();
}

auto AssetProxy::type() const -> std::string {
	if (!m_handle.hasValue()) {
		return "";
	}
	return std::string(m_handle->type());
}

auto AssetProxy::toString() const -> std::string {
	return std::format("Asset({} #{})", m_handle.path(), m_handle.uid().get());
}

namespace {

auto camelToSnake(std::string_view name) -> std::string {
	std::string result;
	result.reserve(name.size() + 4);
	for (char c : name) {
		if (std::isupper(static_cast<unsigned char>(c)) && !result.empty()) {
			result += '_';
		}
		result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return result;
}

auto expectedTypeFor(std::string_view field_type) -> std::string {
	if (field_type.starts_with("std::vector<") && field_type.ends_with('>')) {
		field_type.remove_prefix(12);
		field_type.remove_suffix(1);
	}
	constexpr std::string_view prefix = "assets::Handle<";
	if (!field_type.starts_with(prefix) || !field_type.ends_with('>')) {
		return {};
	}
	field_type.remove_prefix(prefix.size());
	field_type.remove_suffix(1);
	if (auto colon = field_type.rfind(':'); colon != std::string_view::npos) {
		field_type = field_type.substr(colon + 1);
	}
	// Prefab::type() returns "node", not "prefab"
	if (field_type == "Prefab") {
		return "node";
	}
	return camelToSnake(field_type);
}

}

auto AssetProxy::checkType(std::string_view field_type) const -> std::string {
	if (m_handle.uid().data() == 0) {
		return {};
	}
	const std::string expected = expectedTypeFor(field_type);
	if (expected.empty()) {
		return {};
	}
	const std::string actual = m_handle.hasValue() ? std::string(m_handle->type()) : assets::typeOf(m_handle.uid());
	if (actual.empty()) {
		TOAST_WARN("Lua", "checkType: asset {} has no manifest entry; cannot validate against '{}'", m_handle.uid(), field_type);
		return {};
	}
	if (actual != expected) {
		return std::format("expected Asset<{}>, got Asset<{}>", expected, actual);
	}
	return {};
}

}
