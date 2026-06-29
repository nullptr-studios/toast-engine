namespace editor.Components.Modals;

public class AssetTree : PickerWindow {
	public AssetTree(string? assetType = null) : base(new AssetTreeViewModel(assetType)) { }
}
