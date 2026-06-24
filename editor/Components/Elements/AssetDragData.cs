//
// AssetDragData.cs by Xein
// 24 Jun 2026
//

using Avalonia.Input;
using editor.Assets;

namespace editor.Components.Elements;

public record AssetDragRef(string Uid, FileType Type, string Name);

public static class AssetDragData {
	public static readonly DataFormat<AssetDragRef> Format =
		DataFormat.CreateInProcessFormat<AssetDragRef>("toast-asset");
}
