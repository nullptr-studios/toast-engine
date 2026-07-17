namespace editor.Components.Modals;

public class AssetList : PickerWindow {
	public AssetList(string? assetType = null, string? extraType = null)
		: base(new AssetListPickerViewModel(assetType, extraType)) { }
}
