using System.IO;
using Avalonia.Media;
using Avalonia.Media.Imaging;

namespace editor.Assets;

public enum FileType {
	Unknown,
	Node,
	Texture,
	Model,
	Material,
	Shader,
	Script
}

public class AssetFile {
	private Bitmap? m_thumbnail;
	private bool m_thumbnailChecked;

	public AssetFile(string path) {
		Filepath = Path.GetFullPath(path);
		var name = Path.GetFileNameWithoutExtension(path);
		// strip both extensions (e.g. "foo.ktx2.meta" -> name is "foo", ext is ".ktx2")
		Name = Path.GetFileNameWithoutExtension(name);
		Type = Path.GetExtension(name).ToLowerInvariant() switch {
			".tnode" => FileType.Node,
			".ktx2" => FileType.Texture,
			".tmesh" => FileType.Model,
			".tmat" => FileType.Material,
			".slang" => FileType.Shader,
			".lua" => FileType.Script,
			_ => FileType.Unknown
		};
	}

	public string Name { get; }
	public string Filepath { get; }
	public FileType Type { get; }
	public string TypeLabel => Type.ToString();

	public Bitmap? Thumbnail {
		get {
			// lazy loaded and cached -> reading from disk on every property access would be too slow
			if (m_thumbnailChecked) return m_thumbnail;
			m_thumbnailChecked = true;
			if (Type != FileType.Texture || !ProjectContext.IsInitialized) return null;
			var header = MetaFile.ReadHeader(Filepath);
			if (header is null) return null;
			var thumbPath = Path.Combine(ProjectContext.CachePath, "thumbnails", header.Uid + ".png");
			if (!File.Exists(thumbPath)) return null;
			try {
				m_thumbnail = new Bitmap(thumbPath);
			} catch {
				/* ignore */
			}

			return m_thumbnail;
		}
	}

	public bool HasThumbnail => Thumbnail is not null;

	public IBrush TypeColor =>
		Type switch {
			FileType.Node => new SolidColorBrush(Color.Parse("#6495ED")),
			FileType.Texture => new SolidColorBrush(Color.Parse("#3CB371")),
			FileType.Model => new SolidColorBrush(Color.Parse("#FF8C00")),
			FileType.Material => new SolidColorBrush(Color.Parse("#9370DB")),
			FileType.Shader => new SolidColorBrush(Color.Parse("#00CED1")),
			FileType.Script => new SolidColorBrush(Color.Parse("#FFD700")),
			_ => new SolidColorBrush(Color.Parse("#696969"))
		};
}
