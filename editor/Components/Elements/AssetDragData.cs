using System.Collections.Generic;
using Avalonia.Input;

namespace editor.Components.Elements;

public record AssetDragRef(string Uid, string Type, string Name);

public static class AssetDragData {
	public static readonly DataFormat<AssetDragRef> Format =
		DataFormat.CreateInProcessFormat<AssetDragRef>("toast-asset");

	public static readonly DataFormat<IReadOnlyList<string>> ItemsFormat =
		DataFormat.CreateInProcessFormat<IReadOnlyList<string>>("toast-asset-items");

	public static readonly DataFormat<IReadOnlyList<AssetDragRef>> MultiFormat =
		DataFormat.CreateInProcessFormat<IReadOnlyList<AssetDragRef>>("toast-assets");
}
