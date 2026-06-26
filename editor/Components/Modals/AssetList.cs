namespace editor.Components.Modals;

public class AssetList : PickerWindow {
	public AssetList(string? assetType = null) : base(new AssetListPickerViewModel(assetType)) { }
}
