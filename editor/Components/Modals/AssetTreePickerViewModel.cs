using System.Collections.ObjectModel;
using System.IO;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets;

namespace editor.Components.Modals;

public partial class AssetFolderNode : ObservableObject {
	[ObservableProperty] private bool m_isExpanded = true;

	public AssetFolderNode(string realPath) {
		RealPath = realPath;
		Name = Path.GetFileName(realPath) is { Length: > 0 } n ? n : realPath;

		foreach (var dir in Directory.EnumerateDirectories(realPath))
			Children.Add(new AssetFolderNode(dir));
	}

	public string Name { get; }
	public string RealPath { get; }
	public ObservableCollection<AssetFolderNode> Children { get; } = [];
}

public partial class AssetTreePickerViewModel : ObservableObject {
	[ObservableProperty] private AssetFolderNode? m_selectedFolder;

	public AssetTreePickerViewModel() {
		if (ProjectContext.IsInitialized)
			Roots.Add(new AssetFolderNode(ProjectContext.AssetsPath));
	}

	public ObservableCollection<AssetFolderNode> Roots { get; } = [];

	public string? ResultPath => SelectedFolder is not null ? ProjectContext.ToVirtual(SelectedFolder.RealPath) : null;
}
