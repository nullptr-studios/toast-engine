#include "types.hpp"

namespace assets {
void Asset::addRef() noexcept {
	m_ref_count.fetch_add(1, std::memory_order_relaxed);
}

void Asset::release() noexcept {
	m_ref_count.fetch_sub(1, std::memory_order_relaxed);
}

auto Asset::refCount() const noexcept -> uint32_t {
	return m_ref_count.load(std::memory_order_relaxed);
}

HandleBase::HandleBase(Asset* asset) : m_asset(asset) {
	if (m_asset) {
		m_asset->addRef();
	}
}

HandleBase::HandleBase(Asset* asset, toast::UID uid, std::string_view uri) : m_asset(asset), m_uid(uid), m_uri(uri) {
	if (m_asset) {
		m_asset->addRef();
	}
}

HandleBase::~HandleBase() {
	if (m_asset) {
		m_asset->release();
	}
}

HandleBase::HandleBase(const HandleBase& other) : m_asset(other.m_asset), m_uid(other.m_uid) {
	if (m_asset) {
		m_asset->addRef();
		dispatchOnChange();
	}
}

auto HandleBase::operator=(const HandleBase& other) -> HandleBase& {
	if (this != &other) {
		if (m_asset) {
			m_asset->release();
		}
		m_asset = other.m_asset;
		m_uid = other.m_uid;
		m_uri = other.m_uri;
		if (m_asset) {
			m_asset->addRef();
			dispatchOnChange();
		}
	}
	return *this;
}

HandleBase::HandleBase(HandleBase&& other) noexcept : m_asset(other.m_asset), m_uid(other.m_uid), m_uri(other.m_uri) {
	dispatchOnChange();
	other.m_asset = nullptr;
}

auto HandleBase::operator=(HandleBase&& other) noexcept -> HandleBase& {
	if (this != &other) {
		if (m_asset) {
			m_asset->release();
		}
		m_asset = other.m_asset;
		m_uid = other.m_uid;
		m_uri = other.m_uri;
		other.m_asset = nullptr;
		dispatchOnChange();
	}
	return *this;
}

void HandleBase::onChangeCallback(std::function<void()>&& callback) {
	m_callbacks.emplace_back(callback);
}

auto HandleBase::hasValue() const noexcept -> bool {
	return m_asset != nullptr;
}

auto HandleBase::get() noexcept -> Asset& {
	return *m_asset;
}

auto HandleBase::get() const noexcept -> const Asset& {
	return *m_asset;
}

auto HandleBase::operator->() noexcept -> Asset* {
	return m_asset;
}

auto HandleBase::operator->() const noexcept -> const Asset* {
	return m_asset;
}

void HandleBase::dispatchOnChange() {
	// HACK: This can lead to a racist condition
	for (auto& callback : m_callbacks) {
		callback();
	}
}

auto Texture::get() const noexcept -> const std::vector<uint8_t>& {
	return m_data;
}

}
