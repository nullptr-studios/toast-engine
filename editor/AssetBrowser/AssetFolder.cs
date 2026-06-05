using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;

namespace editor.AssetBrowser;

public class AssetFolder : INotifyPropertyChanged {
    public event PropertyChangedEventHandler? PropertyChanged;
    void Notify([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    public string      Name      { get; set; } = "";
    public string      Filepath  { get; set; } = "";
    public AssetFolder? Parent   { get; }

    private bool m_isExpanded;
    public bool IsExpanded {
        get => m_isExpanded;
        set { m_isExpanded = value; Notify(); }
    }

    public ObservableCollection<AssetFolder> SubFolders { get; } = [];
    public ObservableCollection<AssetFile>   Files      { get; } = [];

    public AssetFolder(string path, AssetFolder? parent = null) {
        Parent = parent;
        var dirInfo = new DirectoryInfo(Path.GetFullPath(path));
        Name     = dirInfo.Name;
        Filepath = dirInfo.FullName;
        foreach (var sub  in dirInfo.EnumerateDirectories())
            SubFolders.Add(new AssetFolder(sub.FullName, this));
        foreach (var file in dirInfo.EnumerateFiles()) {
            if (file.Extension != ".meta") continue;
            Files.Add(new AssetFile(file.FullName));
        }
    }
}
