using System;
using editor.Assets;

namespace editor.Components.Modals;

public class AssetTree : PickerWindow {
	public AssetTree(string? assetType = null) : base(new AssetTreeViewModel(
		assetType is not null && Enum.TryParse<FileType>(assetType, true, out var ft) ? ft : null)) { }
}
