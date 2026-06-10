#include "types.hpp"

#include <sstream>

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

AssetHandleBase::~AssetHandleBase() {
	if (m_asset) {
		m_asset->release();
	}
}

AssetHandleBase::AssetHandleBase(const AssetHandleBase& other) : m_asset(other.m_asset) {
	if (m_asset) {
		m_asset->addRef();
	}
}

auto AssetHandleBase::operator=(const AssetHandleBase& other) -> AssetHandleBase& {
	if (this != &other) {
		if (m_asset) {
			m_asset->release();
		}
		m_asset = other.m_asset;
		if (m_asset) {
			m_asset->addRef();
		}
	}
	return *this;
}

auto AssetHandleBase::operator=(AssetHandleBase&& other) noexcept -> AssetHandleBase& {
	if (this != &other) {
		if (m_asset) {
			m_asset->release();
		}
		m_asset = other.m_asset;
		other.m_asset = nullptr;
	}
	return *this;
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

auto Texture::get() const noexcept -> const std::vector<uint8_t>& {
	return m_data;
}

auto Data::get() const noexcept -> const toml::table& {
	return m_table;
}

auto Data::serialize(SaveMode) const -> std::vector<uint8_t> {
	std::ostringstream ss;
	ss << m_table;
	auto str = ss.str();
	return std::vector<uint8_t>(str.begin(), str.end());
}
}
