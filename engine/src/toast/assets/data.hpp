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
	explicit Data(const toml::table& table, AssetHandle<Schema> schema = {});

	auto type() const -> std::string_view override { return "data"; }

	auto operator[](std::string_view k) -> DataValue& { return m_root[k]; }

	auto operator[](std::string_view k) const -> const DataValue& { return m_root[k]; }

	auto contains(std::string_view k) const -> bool { return m_root.contains(k); }

	auto root() const -> const DataValue& { return m_root; }

	auto schema() const -> const AssetHandle<Schema>& { return m_schema; }

	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

protected:
	DataValue m_root;                ///< Object DataValue holding all fields
	AssetHandle<Schema> m_schema;    ///< optional

private:
	static auto buildRoot(const toml::table& table, const Schema* schema) -> DataValue;
};

}
