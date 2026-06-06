/**
 * @file asset_types.hpp
 * @author Xein
 * @date 06 Jun 2026
 *
 * @brief Concrete asset types for the Toast engine
 */

#pragma once

#include <toast/export.hpp>
#include <toml++/toml.hpp>
#include <vector>

namespace assets {

/**
 * @brief Base class for all engine assets
 */
class TOAST_API Asset {
public:
	Asset() = default;
	virtual ~Asset() = default;

	Asset(const Asset&) = delete;
	auto operator=(const Asset&) -> Asset& = delete;

	[[nodiscard]]
	auto refCount() const noexcept -> uint32_t;

	[[nodiscard]]
	virtual auto type() const -> std::string_view = 0;

protected:
	void addRef() noexcept;
	void release() noexcept;

	friend class AssetHandleBase;

private:
	std::atomic<uint32_t> m_ref_count {0};
};

/**
 * @brief Base class for type-erased asset handles
 */
class TOAST_API AssetHandleBase {
public:
	AssetHandleBase() = default;
	explicit AssetHandleBase(Asset* asset);
	virtual ~AssetHandleBase();

	AssetHandleBase(const AssetHandleBase& other);
	auto operator=(const AssetHandleBase& other) -> AssetHandleBase&;

	AssetHandleBase(AssetHandleBase&& other) noexcept : m_asset(other.m_asset) { other.m_asset = nullptr; }

	auto operator=(AssetHandleBase&& other) noexcept -> AssetHandleBase&;

	[[nodiscard]]
	auto hasValue() const noexcept -> bool;

	[[nodiscard]]
	auto get() const noexcept -> const Asset&;

	[[nodiscard]]
	auto operator->() const noexcept -> const Asset* {
		return m_asset;
	}

protected:
	Asset* m_asset = nullptr;
};

/**
 * @brief Type-safe smart pointer for assets
 */
template<typename T>
class AssetHandle : public AssetHandleBase {
public:
	using AssetHandleBase::AssetHandleBase;

	[[nodiscard]]
	auto operator->() const noexcept -> const T* {
		return static_cast<const T*>(this->m_asset);
	}

	[[nodiscard]]
	auto operator*() const noexcept -> const T& {
		return *static_cast<const T*>(this->m_asset);
	}

	[[nodiscard]]
	auto get() const noexcept -> const T& {
		return *static_cast<const T*>(this->m_asset);
	}
};

/**
 * @brief Asset representing a texture, currently holding raw KTX2 bytes
 */
class TOAST_API Texture : public Asset {
public:
	explicit Texture(std::vector<uint8_t> data) : m_data(std::move(data)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "texture";
	}

	[[nodiscard]]
	auto get() const noexcept -> const std::vector<uint8_t>&;

private:
	std::vector<uint8_t> m_data;
};

/**
 * @brief Asset representing parsed TOML data
 */
class TOAST_API Data : public Asset {
public:
	explicit Data(toml::table table) : m_table(std::move(table)) { }

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "data";
	}

	[[nodiscard]]
	auto get() const noexcept -> const toml::table&;

private:
	toml::table m_table;
};

}
