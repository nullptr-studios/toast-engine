/**
 * @file material_instance.hpp
 * @author Xein
 * @date 17 Jul 2026
 *
 * @brief Material instance asset
 */

#pragma once
#include "material.hpp"

#include <string>
#include <unordered_map>

namespace assets {

/**
 * @class MaterialInstance
 * @brief Mirrors a parent Material's parameters, overriding only the values it sets
 */
class TOAST_API MaterialInstance : public Material {
public:
	explicit MaterialInstance(const toml::table& table, AssetHandle<Schema> schema = {});
	~MaterialInstance() override;

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "material_instance";
	}

	[[nodiscard]]
	auto serialize(SaveMode mode) const -> std::vector<uint8_t> override;

	void reload(const toml::table& table) override;

	[[nodiscard]]
	auto parent() const -> const AssetHandle<Material>&;    ///< @brief The parent material

	[[nodiscard]]
	auto shaders() const -> const std::vector<AssetHandle<Shader>>& override;

	[[nodiscard]]
	auto settings() const -> MaterialSettings override;

	[[nodiscard]]
	auto value(std::string_view key) const -> const DataValue* override;

	[[nodiscard]]
	auto rootMaterial() -> Material* override;

private:
	auto parentMaterial() const -> Material*;

	[[nodiscard]]
	auto deltaRoot() const -> DataValue;

	mutable AssetHandle<Material> m_parent;
	mutable bool m_parent_dirty = true;
	mutable std::unordered_map<std::string, DataValue> m_merged_objects;
};

}
