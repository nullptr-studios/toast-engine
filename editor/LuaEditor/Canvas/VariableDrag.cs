using Avalonia.Input;
using editor.LuaEditor.Editing;

namespace editor.LuaEditor.Canvas;

public sealed record HierarchyDrag(GraphSession Session, VariableRow Row);

public static class VariableDrag {
	public static readonly DataFormat<HierarchyDrag> Format = DataFormat.CreateInProcessFormat<HierarchyDrag>("lua-graph-variable");
}
