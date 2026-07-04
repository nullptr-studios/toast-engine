//
// ArrayItemDragData.cs by Xein
// 24 Jun 2026
//

using System.Collections;
using Avalonia.Input;

namespace editor.Components.Elements;

public record ArrayDragRef(ArrayBox Owner, object Item);

public static class ArrayItemDragData {
	public static readonly DataFormat<ArrayDragRef> Format =
		DataFormat.CreateInProcessFormat<ArrayDragRef>("toast-array-item");
}

public interface IRowSplittable {
	bool ShouldSplitRow { get; }
}

/// Items can opt out of being shown as a row
public interface IRowVisible {
	bool RowVisible { get; }
}

/// A struct (object) array element: renders as one Bg3 card with its first key in
/// the header row (beside grip/X) and the remaining keys stacked below
public interface IStructRow {
	bool IsStructRow { get; }
	IList StructKeys { get; }
}
