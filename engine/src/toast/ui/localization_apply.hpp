/**
 * @file localization_apply.hpp
 * @author Xein
 * @date 18 Jul 2026
 *
 * @brief Applies localized image references to a loaded document
 */

#pragma once

// NOLINTNEXTLINE(readability-identifier-naming)
namespace Rml {
class ElementDocument;
}

namespace ui {

void applyImageLocalization(Rml::ElementDocument* document);

}
