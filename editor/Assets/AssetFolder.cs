using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;

namespace editor.Assets;

public class AssetFolder : INotifyPropertyChanged {
	private bool m_isExpanded;
	private bool m_isSelected;

	public AssetFolder(string path, AssetFolder? parent = null) {
		Parent = parent;
		var dirInfo = new DirectoryInfo(Path.GetFullPath(path));
		Name = dirInfo.Name;
		Filepath = dirInfo.FullName;
		foreach (var sub in dirInfo.EnumerateDirectories())
			SubFolders.Add(new AssetFolder(sub.FullName, this));
		foreach (var file in dirInfo.EnumerateFiles()) {
			if (file.Extension != ".meta") continue; // assets are tracked by their .meta sidecar
			Files.Add(new AssetFile(file.FullName));
		}
	}

	public string Name { get; set; } = "";
	public string Filepath { get; set; } = "";
	public AssetFolder? Parent { get; }
	public AssetBrowserViewModel? Owner { get; set; }

	public bool IsExpanded {
		get => m_isExpanded;
		set {
			m_isExpanded = value;
			Notify();
		}
	}

	public bool IsSelected {
		get => m_isSelected;
		set {
			m_isSelected = value;
			Notify();
		}
	}

	public ObservableCollection<AssetFolder> SubFolders { get; } = [];
	public ObservableCollection<AssetFile> Files { get; } = [];
	public event PropertyChangedEventHandler? PropertyChanged;

	private void Notify([CallerMemberName] string? name = null) {
		PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
	}
}
