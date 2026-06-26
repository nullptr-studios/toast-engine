using System.Collections.Generic;
using editor.Workspace;

namespace editor.Components.Modals;

public class HierarchyTree : PickerWindow {
	public HierarchyTree(IEnumerable<HierarchyElement> roots, HierarchyElement? exclude = null,
		string? allowedType = null)
		: base(new HierarchyPickerViewModel(roots, exclude, allowedType)) { }
}
