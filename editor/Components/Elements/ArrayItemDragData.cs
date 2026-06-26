//
// ArrayItemDragData.cs by Xein
// 24 Jun 2026
//

using Avalonia.Input;

namespace editor.Components.Elements;

public record ArrayDragRef(ArrayBox Owner, object Item);

public static class ArrayItemDragData {
	public static readonly DataFormat<ArrayDragRef> Format =
		DataFormat.CreateInProcessFormat<ArrayDragRef>("toast-array-item");
}
