using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;

namespace editor.Components.Modals;

// Static API for picking a folder or asset from the project tree
public static class AssetTreePicker {
	public static Task<string?> PickFolder(Window owner, string? title = null) {
		var window = new AssetTreePickerWindow();
		if (title is not null) window.Title = title;
		return window.ShowDialog<string?>(owner);
	}

	// Pick any asset file — folder-only mode used for now; full asset mode added in Phase 5
	public static Task<string?> PickAsset(Window owner, FilePickerFileType? filter = null, string? title = null) {
		return PickFolder(owner, title ?? "Select Asset");
	}
}
