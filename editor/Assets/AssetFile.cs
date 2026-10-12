using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using System.Windows.Input;
using Avalonia;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Styling;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.Input;
using editor.Assets.Types;
using editor.Git;
using ImageMagick;
using Lucide.Avalonia;

namespace editor.Assets;

public class AssetFile : INotifyPropertyChanged {
	private static readonly IBrush s_unknownBrush = new SolidColorBrush(Color.Parse("#696969"));

	private static readonly ConcurrentDictionary<string, (DateTime Stamp, MetaHeader? Header)> s_headerCache =
		new(StringComparer.OrdinalIgnoreCase);

	private MetaHeader? m_header;
	private bool m_headerChecked;
	private bool m_isSelected;
	private IReadOnlyList<string>? m_tagIds;

	private Bitmap? m_thumbnail;
	private bool m_thumbnailChecked;
	private Bitmap? m_grayThumbnail;
	private bool m_grayRequested;

	public AssetFile(string path) {
		Filepath = Path.GetFullPath(path);
		// strip both extensions: "foo.ktx2.meta" → inner name "foo.ktx2", full ext ".ktx2"
		var inner = Path.GetFileNameWithoutExtension(path);
		var ext = AssetTypeRegistry.GetExtension(inner);
		Name = inner[..^ext.Length];
		Definition = AssetTypeRegistry.ByExtension(ext);
		RemoveTagCommand = new RelayCommand<AssetTag>(tag => {
			if (tag is not null) Owner?.SetTag([this], tag, false);
		});
	}

	/// <summary>
	/// A file that is not a tracked asset it has no .meta sidecar, no UID and no thumbnail
	/// </summary>
	private AssetFile(string path, bool raw) {
		IsRaw = raw;
		Filepath = Path.GetFullPath(path);
		Name = Path.GetFileName(path);
		Definition = AssetTypeRegistry.ByExtension(AssetTypeRegistry.GetExtension(Name));
		RemoveTagCommand = new RelayCommand<AssetTag>(_ => { });
	}

	public static AssetFile Raw(string path) {
		return new AssetFile(path, true);
	}

	// True for a cache:// entry
	public bool IsRaw { get; }

	public string Name { get; }
	public string Filepath { get; }
	public BaseAsset? Definition { get; }
	public LucideIconKind Icon => Definition?.Icon ?? LucideIconKind.OctagonAlert;
	public string TypeLabel =>
		Definition?.ChipText ?? (IsRaw ? Path.GetExtension(Name).TrimStart('.').ToUpperInvariant() : "?");

	public AssetBrowserViewModel? Owner { get; set; }

	public bool CanModify =>
		ProjectContext.IsInitialized &&
		!ProjectContext.IsUnderCore(Filepath) &&
		ProjectContext.IsUnderContentDatabase(Filepath) &&
		!IsLockedByOthers;

	public bool CanTag => !IsRaw && CanModify;

	public string AssetPath => IsRaw ? Filepath : Filepath[..^".meta".Length];

	public GitFileStatus GitStatus => GitService.Current?.GetAssetStatus(AssetPath) ?? GitFileStatus.None;

	public GitLock? LockInfo => GitService.Current?.GetLock(AssetPath);
	public bool IsLockedByMe => LockInfo is { OwnedByMe: true };
	public bool IsLockedByOthers => LockInfo is { OwnedByMe: false };
	public bool IsLockable => GitService.Current?.IsLockable(AssetPath) == true;
	public bool CanLock => !IsRaw && LockInfo is null && IsLockable;
	public bool CanUnlock => IsLockedByMe;

	// A file someone else holds the lock on shows a lock instead of its type icon, and a grayscale preview
	public LucideIconKind DisplayIcon => IsLockedByOthers ? LucideIconKind.Lock : Icon;
	public IBrush DisplayIconBrush => IsLockedByOthers ? GitStatusStyle.Orange : TypeColor;
	public bool ShowLockOverlay => IsLockedByOthers && HasThumbnail;
	public Bitmap? DisplayThumbnail => IsLockedByOthers && m_grayThumbnail is not null ? m_grayThumbnail : Thumbnail;

	public void NotifyGitChanged() {
		Notify(nameof(GitStatus));
		Notify(nameof(LockInfo));
		Notify(nameof(IsLockedByMe));
		Notify(nameof(IsLockedByOthers));
		Notify(nameof(IsLockable));
		Notify(nameof(CanLock));
		Notify(nameof(CanUnlock));
		Notify(nameof(DisplayIcon));
		Notify(nameof(DisplayIconBrush));
		Notify(nameof(ShowLockOverlay));
		Notify(nameof(DisplayThumbnail));
		Notify(nameof(CanModify));
		Notify(nameof(CanTag));
		if (IsLockedByOthers) LoadGrayThumbnail();
	}

	private void LoadGrayThumbnail() {
		if (m_grayRequested || IsRaw || Definition?.HasThumbnail != true || !ProjectContext.IsInitialized) return;
		m_grayRequested = true;

		var filepath = Filepath;
		Task.Run(() => {
			var header = ReadHeaderCached(filepath);
			if (header is null) return;
			var thumbPath = Path.Combine(ProjectContext.CachePath, "thumbnails", header.Uid + ".png");
			if (!File.Exists(thumbPath)) return;
			try {
				using var image = new MagickImage(thumbPath);
				image.Grayscale();
				using var stream = new MemoryStream(image.ToByteArray(MagickFormat.Png));
				var bmp = new Bitmap(stream);
				Dispatcher.UIThread.Post(() => {
					m_grayThumbnail = bmp;
					Notify(nameof(DisplayThumbnail));
				});
			} catch {
				/* ignore */
			}
		});
	}

	public bool IsSelected {
		get => m_isSelected;
		set {
			m_isSelected = value;
			Notify();
		}
	}

	public MetaHeader? Header {
		get {
			if (m_headerChecked) return m_header;
			m_headerChecked = true;
			m_header = IsRaw ? null : ReadHeaderCached(Filepath);
			return m_header;
		}
	}

	public string? Uid => Header?.Uid;

	public string ModifiedAt => Header?.ModifiedAt ?? "";

	public IReadOnlyList<string> TagIds => m_tagIds ??= Header?.Tags ?? [];

	public IReadOnlyList<AssetTag> Tags => AssetBrowserSettings.Resolve(TagIds);

	public ICommand RemoveTagCommand { get; }

	public Bitmap? Thumbnail {
		get {
			if (m_thumbnailChecked) return m_thumbnail;
			m_thumbnailChecked = true;
			if (IsRaw || Definition?.HasThumbnail != true || !ProjectContext.IsInitialized) return null;

			var filepath = Filepath;
			Task.Run(() => {
				var header = ReadHeaderCached(filepath);
				if (header is null) return;
				var thumbPath = Path.Combine(ProjectContext.CachePath, "thumbnails", header.Uid + ".png");
				if (!File.Exists(thumbPath)) return;
				try {
					var bmp = new Bitmap(thumbPath);
					Dispatcher.UIThread.Post(() => {
						m_thumbnail = bmp;
						Notify();
						Notify(nameof(HasThumbnail));
						Notify(nameof(DisplayThumbnail));
						Notify(nameof(ShowLockOverlay));
					});
				} catch {
					/* ignore */
				}
			});

			return null;
		}
	}

	public bool HasThumbnail => Thumbnail is not null;

	public void RefreshThumbnail() {
		m_thumbnailChecked = false;
		m_thumbnail = null;
		m_grayThumbnail = null;
		m_grayRequested = false;
		Notify(nameof(Thumbnail));
		Notify(nameof(HasThumbnail));
		Notify(nameof(DisplayThumbnail));
		if (IsLockedByOthers) LoadGrayThumbnail();
	}

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

	public bool WriteTags(IReadOnlyCollection<string> tagIds) {
		if (!CanTag || !MetaFile.SetTags(Filepath, tagIds)) return false;
		s_headerCache.TryRemove(Filepath, out _);
		m_tagIds = [..tagIds];
		NotifyTagsChanged();
		return true;
	}

	public void NotifyTagsChanged() {
		Notify(nameof(TagIds));
		Notify(nameof(Tags));
	}

	private static MetaHeader? ReadHeaderCached(string metaPath) {
		DateTime stamp;
		try {
			stamp = File.GetLastWriteTimeUtc(metaPath);
		} catch {
			return null;
		}

		if (s_headerCache.TryGetValue(metaPath, out var cached) && cached.Stamp == stamp) return cached.Header;
		var header = MetaFile.ReadHeader(metaPath);
		s_headerCache[metaPath] = (stamp, header);
		return header;
	}

	private void Notify([CallerMemberName] string? name = null) {
		PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
	}
}
