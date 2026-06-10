using System.Collections.ObjectModel;
using System.IO;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Services;

namespace editor.Import;

public partial class FolderNode : ObservableObject {
	[ObservableProperty] private bool m_isExpanded = true;

	public FolderNode(string realPath) {
		RealPath = realPath;
		Name = Path.GetFileName(realPath) is { Length: > 0 } n ? n : realPath;

		foreach (var dir in Directory.EnumerateDirectories(realPath)) Children.Add(new FolderNode(dir));
	}

	public string Name { get; }
	public string RealPath { get; }
	public ObservableCollection<FolderNode> Children { get; } = [];
}

public partial class AssetFolderPickerViewModel : ObservableObject {
	[ObservableProperty] private FolderNode? m_selectedFolder;

	public AssetFolderPickerViewModel() {
		if (ProjectContext.IsInitialized)
			Roots.Add(new FolderNode(ProjectContext.AssetsPath));
	}

	public ObservableCollection<FolderNode> Roots { get; } = [];

	public string? ResultPath => SelectedFolder is not null ? ProjectContext.ToVirtual(SelectedFolder.RealPath) : null;
}
