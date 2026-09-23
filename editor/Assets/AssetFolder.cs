using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;

namespace editor.Assets;

public class AssetFolder : INotifyPropertyChanged {
	private bool m_isExpanded;
	private bool m_isSelected;

	/// <param name="listRawFiles">
	/// List every file rather than only tracked assets
	/// </param>
	public AssetFolder(string path, AssetFolder? parent = null, bool listRawFiles = false) {
		Parent = parent;

		var dirInfo = new DirectoryInfo(Path.GetFullPath(path));
		Name = dirInfo.Name;
		Filepath = dirInfo.FullName;

		foreach (var sub in dirInfo.EnumerateDirectories()
			         .OrderBy(d => d.Name, StringComparer.OrdinalIgnoreCase)
			         .ThenBy(d => d.Name, StringComparer.Ordinal))
			SubFolders.Add(new AssetFolder(sub.FullName, this, listRawFiles));

		foreach (var file in dirInfo.EnumerateFiles()
			         .OrderBy(f => f.Name, StringComparer.OrdinalIgnoreCase)
			         .ThenBy(f => f.Name, StringComparer.Ordinal)) {
			if (listRawFiles) {
				if (file.Extension == ".meta")
				    continue;
				Files.Add(AssetFile.Raw(file.FullName));
				continue;
			}

			if (file.Extension != ".meta") continue; // assets are tracked by their .meta sidecar
			Files.Add(new AssetFile(file.FullName));
		}
	}

	public string Name { get; set; } = "";
	public string Filepath { get; set; } = "";
	public AssetFolder? Parent { get; }
	public AssetBrowserViewModel? Owner { get; set; }

	public bool CanModify =>
		ProjectContext.IsInitialized &&
		!ProjectContext.IsUnderCore(Filepath) &&
		ProjectContext.IsUnderContentDatabase(Filepath) &&
		!ProjectContext.IsDatabaseRoot(Filepath);

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
