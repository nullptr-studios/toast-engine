namespace editor.Components.Modals;

public class AssetFolderTree : PickerWindow {
	public AssetFolderTree(bool useArtwork = false)
		: base(new AssetFolderTreeViewModel(useArtwork)) { }
}
