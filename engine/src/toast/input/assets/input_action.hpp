/**
 * @file input_action.hpp
 * @author Xein
 * @date 21 Jun 2026
 *
 * @brief Asset describing a single input action and its binds
 */

#pragma once
#include <string>
#include <toast/assets/data.hpp>

namespace assets {
// clang-format off

/**
 * @brief Value domain an action operates in
 */
enum class ActionValueType : uint8_t { action_0d, action_1d, action_2d };

/**
 * @brief How an action combines values coming from several binds
 *
 * Highest keeps the bind with the highest absolute value while average
 * just does an average of all of the inputs currently affecting the value
 */
enum class AccumulationType : uint8_t { highest, average };

// clang-format on

/**
 * @brief Asset representing one input action loaded from a .taction file
 */
class TOAST_API Action : public Data {
public:
	explicit Action(const toml::table& table, AssetHandle<Schema> schema = {});

	[[nodiscard]]
	auto type() const -> std::string_view override {
		return "input_action";
	}

	/// @returns A reconstructed TOML table preserving all bind, trigger and modifier entries
	[[nodiscard]]
	auto get() const -> toml::table;
	[[nodiscard]]
	auto name() const noexcept -> std::string_view;            ///< @returns The action's display name
	[[nodiscard]]
	auto functionName() const noexcept -> std::string_view;    ///< @returns The reflected function this action invokes
	[[nodiscard]]
	auto description() const noexcept -> std::string_view;     ///< @returns The action's human-readable description
	[[nodiscard]]
	auto valueType() const noexcept -> ActionValueType;        ///< @returns The value domain (0D, 1D or 2D)
	[[nodiscard]]
	auto accumulation() const noexcept -> AccumulationType;    ///< @returns How values from multiple binds are combined

private:
	std::string m_name;
	std::string m_function_name;
	std::string m_description;
	ActionValueType m_value_type = ActionValueType::action_0d;
	AccumulationType m_accumulation = AccumulationType::highest;
};

}
