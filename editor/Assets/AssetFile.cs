using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using Avalonia;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Styling;
using editor.Assets.Types;

namespace editor.Assets;

public class AssetFile : INotifyPropertyChanged {
	private static readonly IBrush s_unknownBrush = new SolidColorBrush(Color.Parse("#696969"));

	private Bitmap? m_thumbnail;
	private bool m_thumbnailChecked;
	private string? m_uid;
	private bool m_uidChecked;
	private bool m_isSelected;

	public AssetFile(string path) {
		Filepath = Path.GetFullPath(path);
		// strip both extensions: "foo.ktx2.meta" → inner name "foo.ktx2", full ext ".ktx2"
		var inner = Path.GetFileNameWithoutExtension(path);
		var ext = AssetTypeRegistry.GetExtension(inner);
		Name = inner[..^ext.Length];
		Definition = AssetTypeRegistry.ByExtension(ext);
	}

	public string Name { get; }
	public string Filepath { get; }
	public BaseAsset? Definition { get; }
	public string TypeLabel => Definition?.ChipText ?? "?";

	public AssetBrowserViewModel? Owner { get; set; }

	public bool IsSelected {
		get => m_isSelected;
		set {
			m_isSelected = value;
			Notify();
		}
	}

	public string? Uid {
		get {
			if (m_uidChecked) return m_uid;
			m_uidChecked = true;
			m_uid = MetaFile.ReadHeader(Filepath)?.Uid;
			return m_uid;
		}
	}

	public Bitmap? Thumbnail {
		get {
			if (m_thumbnailChecked) return m_thumbnail;
			m_thumbnailChecked = true;
			if (Definition?.HasThumbnail != true || !ProjectContext.IsInitialized) return null;
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

	public IBrush TypeColor {
		get {
			if (Definition is null) return s_unknownBrush;
			if (Application.Current?.TryGetResource(Definition.ChipColor, ThemeVariant.Default, out var res) == true
			    && res is IBrush brush)
				return brush;
			return s_unknownBrush;
		}
	}

	public event PropertyChangedEventHandler? PropertyChanged;

	private void Notify([CallerMemberName] string? name = null) =>
		PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
