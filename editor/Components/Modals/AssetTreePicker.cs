using System.Threading.Tasks;
using Avalonia.Controls;

namespace editor.Components.Modals;

// Static API for picking a folder or asset from the project tree
public static class AssetTreePicker {
	public static Task<string?> PickFolder(Window owner, string? title = null) {
		var window = new AssetFolderTree();
		if (title is not null) window.Title = title;
		return window.ShowDialog<string?>(owner);
	}

	public static Task<string?> PickAsset(Window owner, string? assetType = null, string? title = null) {
		var window = new AssetTree(assetType);
		if (title is not null) window.Title = title;
		return window.ShowDialog<string?>(owner);
	}
}
