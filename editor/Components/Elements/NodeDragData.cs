//
// NodeDragData.cs by Xein
// 24 Jun 2026
//

using Avalonia.Input;
using editor.Workspace;

namespace editor.Components.Elements;

public static class NodeDragData {
	public static readonly DataFormat<HierarchyElement> Format =
		DataFormat.CreateInProcessFormat<HierarchyElement>("toast-node");
}
