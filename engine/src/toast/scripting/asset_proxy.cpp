#include "asset_proxy.hpp"

#include <format>
#include <toast/assets/assets.hpp>
#include <toast/log.hpp>

namespace scripting {

AssetProxy::AssetProxy(toast::UID uid) : m_handle(assets::load(uid)) { }

AssetProxy::AssetProxy(assets::AssetHandleBase handle) : m_handle(std::move(handle)) { }

std::string AssetProxy::path() const {
	return std::string(m_handle.path());
}

toast::UID AssetProxy::uid() const {
	return m_handle.uid();
}

bool AssetProxy::hasValue() const {
	return m_handle.hasValue();
}

std::string AssetProxy::type() const {
	if (!m_handle.hasValue()) {
		return "";
	}
	return std::string(m_handle->type());
}

std::string AssetProxy::toString() const {
	return std::format("Asset({} #{})", m_handle.path(), m_handle.uid().get());
}

namespace {

std::string camelToSnake(std::string_view name) {
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

std::string expectedTypeFor(std::string_view fieldType) {
	if (fieldType.starts_with("std::vector<") && fieldType.ends_with('>')) {
		fieldType.remove_prefix(12);
		fieldType.remove_suffix(1);
	}
	constexpr std::string_view prefix = "assets::AssetHandle<";
	if (!fieldType.starts_with(prefix) || !fieldType.ends_with('>')) {
		return {};
	}
	fieldType.remove_prefix(prefix.size());
	fieldType.remove_suffix(1);
	if (auto colon = fieldType.rfind(':'); colon != std::string_view::npos) {
		fieldType = fieldType.substr(colon + 1);
	}
	// Prefab::type() returns "node", not "prefab"
	if (fieldType == "Prefab") {
		return "node";
	}
	return camelToSnake(fieldType);
}

}

std::string AssetProxy::checkType(std::string_view fieldType) const {
	if (m_handle.uid().data() == 0) {
		return {};
	}
	const std::string expected = expectedTypeFor(fieldType);
	if (expected.empty()) {
		return {};
	}
	const std::string actual = m_handle.hasValue() ? std::string(m_handle->type()) : assets::typeOf(m_handle.uid());
	if (actual.empty()) {
		TOAST_WARN("Lua", "checkType: asset {} has no manifest entry; cannot validate against '{}'", m_handle.uid(), fieldType);
		return {};
	}
	if (actual != expected) {
		return std::format("expected Asset<{}>, got Asset<{}>", expected, actual);
	}
	return {};
}

}
