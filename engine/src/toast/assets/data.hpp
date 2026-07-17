/**
 * @file data.hpp
 * @author Xein
 * @date 10 Jun 2026
 * @brief Typed data asset backed by schema-driven TOML
 */

#pragma once

#include "core_types.hpp"
#include "data_value.hpp"
#include "schema.hpp"

namespace assets {

class TOAST_API Data : public Asset, public ISaveable {
public:
	/// @brief Tag type for the keep-all-keys constructor
	struct KeepAllKeysTag { };

	static constexpr KeepAllKeysTag keep_all_keys {};

	explicit Data(const toml::table& table, AssetHandle<Schema> schema = {});

	/// @brief Stores the schema handle for round-trip serialization but runs free-form buildRoot,
	///        keeping every TOML key
	Data(const toml::table& table, AssetHandle<Schema> schema, KeepAllKeysTag);

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "data";
	}

	[[nodiscard]]
	auto operator[](std::string_view k) -> DataValue& {
		return m_root[k];
	}

	[[nodiscard]]
	auto operator[](std::string_view k) const -> const DataValue& {
		return m_root[k];
	}

	[[nodiscard]]
	auto contains(std::string_view k) const -> bool {
		return m_root.contains(k);
	}

	[[nodiscard]]
	auto root() const -> const DataValue& {
		return m_root;
	}

	[[nodiscard]]
	auto schema() const -> const AssetHandle<Schema>& {
		return m_schema;
	}

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	/// @brief Re-parses the asset contents in place
	virtual void reload(const toml::table& table);

protected:
	DataValue m_root;                ///< Object DataValue holding all fields
	AssetHandle<Schema> m_schema;    ///< optional
	bool m_keep_all_keys = false;

private:
	static auto buildRoot(const toml::table& table, const Schema* schema) -> DataValue;
};

}
