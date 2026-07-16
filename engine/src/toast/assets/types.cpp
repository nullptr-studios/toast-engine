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

AssetHandleBase::AssetHandleBase(Asset* asset) : m_asset(asset) {
	if (m_asset) {
		m_asset->addRef();
	}
}

AssetHandleBase::AssetHandleBase(Asset* asset, toast::UID uid, std::string_view uri) : m_asset(asset), m_uid(uid), m_uri(uri) {
	if (m_asset) {
		m_asset->addRef();
	}
}

AssetHandleBase::~AssetHandleBase() {
	if (m_asset) {
		m_asset->release();
	}
}

AssetHandleBase::AssetHandleBase(const AssetHandleBase& other) : m_asset(other.m_asset), m_uid(other.m_uid) {
	if (m_asset) {
		m_asset->addRef();
		dispatchOnChange();
	}
}

auto AssetHandleBase::operator=(const AssetHandleBase& other) -> AssetHandleBase& {
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

AssetHandleBase::AssetHandleBase(AssetHandleBase&& other) noexcept
    : m_asset(other.m_asset),
      m_uid(other.m_uid),
      m_uri(other.m_uri) {
	dispatchOnChange();
	other.m_asset = nullptr;
}

auto AssetHandleBase::operator=(AssetHandleBase&& other) noexcept -> AssetHandleBase& {
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

void AssetHandleBase::onChangeCallback(std::function<void()>&& callback) {
	m_callbacks.emplace_back(callback);
}

auto AssetHandleBase::hasValue() const noexcept -> bool {
	return m_asset != nullptr;
}

auto AssetHandleBase::get() noexcept -> Asset& {
	return *m_asset;
}

auto AssetHandleBase::get() const noexcept -> const Asset& {
	return *m_asset;
}

auto AssetHandleBase::operator->() noexcept -> Asset* {
	return m_asset;
}

auto AssetHandleBase::operator->() const noexcept -> const Asset* {
	return m_asset;
}

void AssetHandleBase::dispatchOnChange() {
	// HACK: This can lead to a racist condition
	for (auto& callback : m_callbacks) {
		callback();
	}
}

auto Texture::get() const noexcept -> const std::vector<uint8_t>& {
	return m_data;
}

}
