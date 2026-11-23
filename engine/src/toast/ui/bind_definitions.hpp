/**
 * @file bind_definitions.hpp
 * @author Xein
 * @date 22 Nov 2026
 *
 * @brief RmlUi data-model definitions that expose BindValue trees (arrays and objects) to `data-for` and `{{a.b}}`
 */

#pragma once
#include "bind_value.hpp"

#include <RmlUi/Core/DataVariable.h>
#include <toast/export.hpp>

namespace ui {

[[nodiscard]]
auto TOAST_API bindDefinitionFor(const BindValue& value) -> Rml::VariableDefinition*;

[[nodiscard]]
auto TOAST_API bindArrayDefinition() -> Rml::VariableDefinition*;

[[nodiscard]]
auto TOAST_API bindObjectDefinition() -> Rml::VariableDefinition*;

}
