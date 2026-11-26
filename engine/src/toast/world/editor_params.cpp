#include "editor_params.hpp"

namespace toast {

auto editorParams() -> EditorParams& {
	static EditorParams instance;
	return instance;
}

}
