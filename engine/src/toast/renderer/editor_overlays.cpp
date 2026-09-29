#include "editor_overlays.hpp"

namespace renderer {

auto editorOverlays() -> EditorOverlays& {
	static EditorOverlays overlays;
	return overlays;
}

}
