/**
 * @file input_layout.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Asset describing a set of actions grouped into layers
 */

#pragma once
#include <string>
#include <toast/assets/data.hpp>
#include <vector>

namespace assets {

/**
 * @brief Asset representing an input layout loaded from a .tlayout file
 */
class TOAST_API InputLayout : public Data {
public:
	/// @brief Hidden layer that is always present in every layout
	static constexpr std::string_view default_layer = "default";

	/**
	 * @brief One action reference inside a layout
	 */
	struct Entry {
		toast::UID id;                        ///< UID of the referenced asset::Action
		std::vector<std::string> included;    ///< layers this action is whitelisted for; empty means every layer
		std::vector<std::string> excluded;    ///< layers this action is blacklisted from
	};

	// clang-format off
	// if you are asking why it;s because goofy ahh clang format alings
	// the doxygen to the previous documentation -x

	/**
	 * @brief Parses the layout from a TOML table, keeping all keys for round-trip serialization
	 * @param table Parsed contents of the .tlayout file
	 */
	explicit InputLayout(const toml::table& table, Handle<Schema> schema = {});
	// clang-format on

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "input_layout";
	}

	/// @returns The layout's display name
	[[nodiscard]]
	auto name() const noexcept -> std::string_view;

	/// @returns Every layer in the layout
	[[nodiscard]]
	auto layers() const noexcept -> const std::vector<std::string>&;

	/// @returns The action entries declared by this layout
	[[nodiscard]]
	auto entries() const noexcept -> const std::vector<Entry>&;

	/**
	 * @brief Whether an action is active for a given layer under this layout
	 * @param entry The action entry to test
	 * @param layer The active layer name
	 * @return true when the layer passes the entry's included/excluded rules
	 */
	[[nodiscard]]
	static auto isActiveForLayer(const Entry& entry, std::string_view layer) noexcept -> bool;

private:
	std::string m_name;
	std::vector<std::string> m_layers;
	std::vector<Entry> m_entries;
};

}
