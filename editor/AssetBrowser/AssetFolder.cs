using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;

namespace editor.AssetBrowser;

public class AssetFolder : INotifyPropertyChanged {
	private bool m_isExpanded;

	public AssetFolder(string path, AssetFolder? parent = null) {
		Parent = parent;
		var dirInfo = new DirectoryInfo(Path.GetFullPath(path));
		Name = dirInfo.Name;
		Filepath = dirInfo.FullName;
		foreach (var sub in dirInfo.EnumerateDirectories())
			SubFolders.Add(new AssetFolder(sub.FullName, this));
		foreach (var file in dirInfo.EnumerateFiles()) {
			if (file.Extension != ".meta") continue;
			Files.Add(new AssetFile(file.FullName));
		}
	}

	public string Name { get; set; } = "";
	public string Filepath { get; set; } = "";
	public AssetFolder? Parent { get; }

	public bool IsExpanded {
		get => m_isExpanded;
		set {
			m_isExpanded = value;
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
