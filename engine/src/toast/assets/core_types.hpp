/**
 * @file core_types.hpp
 * @author Xein
 * @date 10 Jun 2026
 *
 * @brief TODO: Brief description of the file's purpose
 */

#pragma once

#include <toast/export.hpp>
#include <toast/uid.hpp>
#include <toml++/toml.hpp>
#include <vector>

namespace assets {
// clang-format off
enum class SaveMode : uint8_t { editor, game };

/**
 * @brief Inherit from this class to allow an asset to be saved
 */
class TOAST_API ISaveable {
public:
	[[nodiscard]]
	virtual auto serialize(SaveMode mode) const -> std::vector<uint8_t> = 0;
	virtual ~ISaveable() = default;
};

// clang-format on

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

	/**
	 * @brief Constructs a handle that stores its source UID
	 *
	 * If the pointer is null (unresolved handle), the AssetHandle will still have a uid
	 * so serialization won't explode the engine. It also will make handling missing
	 * assets easier
	 */
	AssetHandleBase(Asset* asset, toast::UID uid);

	virtual ~AssetHandleBase();

	AssetHandleBase(const AssetHandleBase& other);
	auto operator=(const AssetHandleBase& other) -> AssetHandleBase&;

	AssetHandleBase(AssetHandleBase&& other) noexcept : m_asset(other.m_asset), m_uid(other.m_uid) { other.m_asset = nullptr; }

	auto operator=(AssetHandleBase&& other) noexcept -> AssetHandleBase&;

	[[nodiscard]]
	auto hasValue() const noexcept -> bool;

	[[nodiscard]]
	auto uid() const noexcept -> toast::UID {
		return m_uid;
	}

	[[nodiscard]]
	auto get() noexcept -> Asset&;

	[[nodiscard]]
	auto get() const noexcept -> const Asset&;

	[[nodiscard]]
	auto operator->() noexcept -> Asset*;

	[[nodiscard]]
	auto operator->() const noexcept -> const Asset*;

protected:
	Asset* m_asset = nullptr;
	toast::UID m_uid;
};

/**
 * @brief Type-safe smart pointer for assets
 */
template<typename T>
class AssetHandle : public AssetHandleBase {
public:
	using asset_type = T;
	using AssetHandleBase::AssetHandleBase;

	[[nodiscard]]
	auto operator->() noexcept -> T* {
		return static_cast<T*>(this->m_asset);
	}

	[[nodiscard]]
	auto operator->() const noexcept -> const T* {
		return static_cast<const T*>(this->m_asset);
	}

	[[nodiscard]]
	auto operator*() noexcept -> T& {
		return *static_cast<T*>(this->m_asset);
	}

	[[nodiscard]]
	auto operator*() const noexcept -> const T& {
		return *static_cast<const T*>(this->m_asset);
	}

	[[nodiscard]]
	auto get() noexcept -> T& {
		return *static_cast<T*>(this->m_asset);
	}

	[[nodiscard]]
	auto get() const noexcept -> const T& {
		return *static_cast<const T*>(this->m_asset);
	}
};
}
