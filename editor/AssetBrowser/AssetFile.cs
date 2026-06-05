using System.IO;
using Avalonia.Media;

namespace editor.AssetBrowser;

public enum FileType {
    Unknown,
    Node,
    Texture,
    Model,
    Material,
    Shader,
    Script,
}

public class AssetFile {
    public string   Name      { get; }
    public string   Filepath  { get; }
    public FileType Type      { get; }
    public string   TypeLabel => Type.ToString();

    public IBrush TypeColor => Type switch {
        FileType.Node     => new SolidColorBrush(Color.Parse("#6495ED")),
        FileType.Texture  => new SolidColorBrush(Color.Parse("#3CB371")),
        FileType.Model    => new SolidColorBrush(Color.Parse("#FF8C00")),
        FileType.Material => new SolidColorBrush(Color.Parse("#9370DB")),
        FileType.Shader   => new SolidColorBrush(Color.Parse("#00CED1")),
        FileType.Script   => new SolidColorBrush(Color.Parse("#FFD700")),
        _                 => new SolidColorBrush(Color.Parse("#696969")),
    };

    public AssetFile(string path) {
        Filepath = Path.GetFullPath(path);
        var name = Path.GetFileNameWithoutExtension(path);
        Name = Path.GetFileNameWithoutExtension(name);
        Type = Path.GetExtension(name).ToLowerInvariant() switch {
            ".tnode" => FileType.Node,
            ".ktx"   => FileType.Texture,
            ".tmesh" => FileType.Model,
            ".tmat"  => FileType.Material,
            ".slang" => FileType.Shader,
            ".lua"   => FileType.Script,
            _        => FileType.Unknown,
        };
    }
}
