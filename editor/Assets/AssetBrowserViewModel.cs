using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Components.Modals;
using editor.Engine;
using Lucide.Avalonia;

namespace editor.Assets;

public sealed class BreadcrumbItem {
	public BreadcrumbItem(string label, Action navigate, bool isLast) {
		Label = label;
		IsLast = isLast;
		Navigate = new RelayCommand(navigate);
	}

	public string Label { get; }
	public bool IsLast { get; }
	public ICommand Navigate { get; }
}

public class AssetBrowserViewModel : Tool, INotifyPropertyChanged {
	private bool m_filterMaterial = true;
	private bool m_filterModel = true;
	private bool m_filterNode = true;
	private bool m_filterScript = true;
	private bool m_filterShader = true;
	private bool m_filterTexture = true;
	private bool m_filterUnknown = true;
	private string m_searchText = "";
	private int m_selectedCount;
	private AssetFolder? m_selectedFolder;
	private AssetFolder? m_preSearchFolder;
	private string? m_refreshTargetPath;

	// clipboard
	private enum ClipMode { None, Copy, Cut }
	private ClipMode m_clipMode;
	private List<string> m_clipPaths = []; // real paths

	// selection
	private readonly HashSet<object> m_selectedItems = [];

	public static AssetBrowserViewModel? Current { get; private set; }

	public AssetBrowserViewModel() {
		Current = this;
		RefreshCommand = new RelayCommand(Refresh);
		ExpandAllCommand = new RelayCommand(() => SetAllExpanded(true));
		CollapseAllCommand = new RelayCommand(() => SetAllExpanded(false));
		CreateFolderCommand = new AsyncRelayCommand(CreateFolder);
		RenameCommand = new AsyncRelayCommand<object>(RenameAsync);
		DeleteCommand = new AsyncRelayCommand<object>(DeleteAsync);
		CopyCommand = new RelayCommand<object>(Copy);
		CutCommand = new RelayCommand<object>(Cut);
		PasteCommand = new RelayCommand(Paste);
		DuplicateCommand = new AsyncRelayCommand<object>(DuplicateAsync);
		NewNodeCommand = new AsyncRelayCommand(() => CreatePrefab("Node", "toast::Node"));
		NewNode3DCommand = new AsyncRelayCommand(() => CreatePrefab("Node3D", "toast::Node3D"));
		NewNodeGenericCommand = new AsyncRelayCommand(() => CreateGenericPrefab());
		LoadFolders();

		// auto-reload whenever the asset database changes
		AssetDatabase.ReloadedDatabase += OnDatabaseReloaded;
	}

	public void RevealAsset(string uid) {
		foreach (var root in Folders)
		foreach (var file in GetAllFiles(root)) {
			if (file.Uid != uid) continue;
			SearchText = "";
			SelectedFolder = FindParentFolder(root, file);
			return;
		}
	}

	public void UpdateSelection(IList? items) {
		m_selectedItems.Clear();
		if (items is not null)
			foreach (var item in items)
				m_selectedItems.Add(item);
		m_selectedCount = m_selectedItems.Count;
		Notify(nameof(ItemCount));
	}

	private async Task CreateFolder() {
		var window = ActiveWindow();
		if (window is null) return;
		var name = await new NewFolderModal().ShowDialog<string?>(window);
		if (string.IsNullOrEmpty(name)) return;
		var parentPath = m_selectedFolder?.Filepath ?? ProjectContext.AssetsPath;
		Directory.CreateDirectory(Path.Combine(parentPath, name));
		Refresh();
	}

	private async Task RenameAsync(object? param) {
		var target = param ?? m_selectedItems.FirstOrDefault();
		switch (target) {
			case AssetFile file:
				await RenameFile(file);
				break;
			case AssetFolder folder:
				await RenameFolder(folder);
				break;
		}
	}

	private async Task RenameFile(AssetFile file) {
		if (!IsEditable(file)) return;
		var window = ActiveWindow();
		if (window is null) return;
		var newName = await new RenameModal(file.Name).ShowDialog<string?>(window);
		if (string.IsNullOrEmpty(newName) || newName == file.Name) return;

		var metaPath = file.Filepath;
		var oldAssetPath = metaPath[..^5];
		var assetExt = Path.GetExtension(oldAssetPath);
		var dir = Path.GetDirectoryName(oldAssetPath)!;
		var newAssetPath = Path.Combine(dir, newName + assetExt);
		var newMetaPath = newAssetPath + ".meta";

		try {
			File.Move(oldAssetPath, newAssetPath);
			File.Move(metaPath, newMetaPath);
			if (file.Type == FileType.Node)
				ToastEngine.RenamePrefabRoot(newAssetPath, newName);
		} catch (Exception ex) {
			await App.Modals.ShowError("Rename failed", ex.Message);
			return;
		}

		AssetDatabase.RebuildAssetDatabase();
	}

	private async Task RenameFolder(AssetFolder folder) {
		if (!IsEditable(folder)) return;
		var window = ActiveWindow();
		if (window is null) return;
		var newName = await new RenameModal(folder.Name).ShowDialog<string?>(window);
		if (string.IsNullOrEmpty(newName) || newName == folder.Name) return;

		var parent = Path.GetDirectoryName(folder.Filepath)!;
		var newPath = Path.Combine(parent, newName);
		try {
			Directory.Move(folder.Filepath, newPath);
		} catch (Exception ex) {
			await App.Modals.ShowError("Rename failed", ex.Message);
			return;
		}

		AssetDatabase.RebuildAssetDatabase();
	}

	private async Task DeleteAsync(object? param) {
		var targets = param is { } p
			? new List<object> { p }
			: m_selectedItems.ToList();

		if (targets.Count == 0) return;

		var label = targets.Count == 1
			? targets[0] switch {
				AssetFile f => $"\"{f.Name}\"",
				AssetFolder d => $"folder \"{d.Name}\" and all its contents",
				_ => "the selected item"
			}
			: $"{targets.Count} items";

		var window = ActiveWindow();
		if (window is null) return;
		var confirmed = await new MessageModal(new ModalConfig(
			"Delete", $"Delete {label}? This cannot be undone.",
			ModalButtons.OkCancel,
			LucideIconKind.Shredder,
			new SolidColorBrush(Color.Parse("#d04040")),
			OkLabel: "Delete",
			OkIcon: LucideIconKind.Shredder
		)).ShowDialog<bool?>(window) == true;
		if (!confirmed) return;

		foreach (var t in targets) {
			switch (t) {
				case AssetFile file when IsEditable(file):
					if (file.Uid is { } uid)
						AssetDatabase.RemoveArtworkOutputs(uid);
					var assetPath = file.Filepath[..^5];
					AssetDatabase.TryDelete(assetPath);
					AssetDatabase.TryDelete(file.Filepath);
					break;
				case AssetFolder folder when IsEditable(folder):
					try { Directory.Delete(folder.Filepath, recursive: true); } catch { /* ignore */ }
					break;
			}
		}

		AssetDatabase.RebuildAssetDatabase();
	}

	private void Copy(object? param) {
		var items = GetTargets(param);
		m_clipPaths = items.OfType<AssetFile>().Select(f => f.Filepath[..^5]).ToList();
		m_clipMode = ClipMode.Copy;
	}

	private void Cut(object? param) {
		var items = GetTargets(param);
		m_clipPaths = items.OfType<AssetFile>().Select(f => f.Filepath[..^5]).ToList();
		m_clipMode = ClipMode.Cut;
	}

	private void Paste() {
		if (m_clipMode == ClipMode.None || m_clipPaths.Count == 0) return;
		var dest = m_selectedFolder?.Filepath ?? ProjectContext.AssetsPath;

		foreach (var src in m_clipPaths) {
			if (!File.Exists(src)) continue;
			var srcMeta = src + ".meta";
			var fileName = Path.GetFileName(src);
			var dstAsset = Path.Combine(dest, fileName);
			var dstMeta = dstAsset + ".meta";

			if (m_clipMode == ClipMode.Cut) {
				// move, keep UID
				try {
					File.Move(src, dstAsset, overwrite: false);
					if (File.Exists(srcMeta)) File.Move(srcMeta, dstMeta, overwrite: false);
				} catch { }
			} else {
				// copy, new UID
				dstAsset = UniqueDestPath(dstAsset);
				dstMeta = dstAsset + ".meta";
				try {
					File.Copy(src, dstAsset);
					CopyMetaWithNewUid(srcMeta, dstMeta);
				} catch { }
			}
		}

		if (m_clipMode == ClipMode.Cut) {
			m_clipPaths.Clear();
			m_clipMode = ClipMode.None;
		}

		AssetDatabase.RebuildAssetDatabase();
	}

	private async Task DuplicateAsync(object? param) {
		var files = GetTargets(param).OfType<AssetFile>().ToList();
		if (files.Count == 0) return;
		foreach (var file in files) {
			if (!IsEditable(file)) continue;
			var src = file.Filepath[..^5];
			var srcMeta = file.Filepath;
			var dir = Path.GetDirectoryName(src)!;
			var stem = Path.GetFileNameWithoutExtension(src);
			var ext = Path.GetExtension(src);
			var dst = UniqueDestPath(Path.Combine(dir, stem + ext));
			var dstMeta = dst + ".meta";
			try {
				File.Copy(src, dst);
				CopyMetaWithNewUid(srcMeta, dstMeta);
			} catch { }
		}
		AssetDatabase.RebuildAssetDatabase();
	}

	private async Task CreateGenericPrefab() {
		var window = ActiveWindow();
		if (window is null) return;
		var popup = new NodeTypeTree();
		var type = await popup.ShowDialog<string?>(window);
		if (type is null) return;

		string defaultName = type[(type.LastIndexOf(':') + 1)..];
		await CreatePrefab(defaultName, type);
	}

	private async Task CreatePrefab(string defaultName, string nodeType) {
		var window = ActiveWindow();
		if (window is null) return;
		var name = await new RenameModal(defaultName).ShowDialog<string?>(window);
		if (string.IsNullOrEmpty(name)) return;

		var dest = m_selectedFolder?.Filepath ?? ProjectContext.AssetsPath;
		var path = UniqueDestPath(Path.Combine(dest, name + ".tnode"));
		try {
			ToastEngine.CreateTNode(path, nodeType);
			MetaFile.Write(path, new MetaHeader { Uid = UidGenerator.Generate(), Type = "node" });
		} catch (Exception ex) {
			await App.Modals.ShowError("Create failed", ex.Message);
			return;
		}
		AssetDatabase.RebuildAssetDatabase();
	}

	public void MoveAsset(string uid, AssetFolder target) {
		if (!AssetDatabase.TryResolve(uid, out var virtualPath, out _)) return;
		var realPath = ProjectContext.Resolve(virtualPath);
		if (!File.Exists(realPath)) return;
		if (!IsEditable(target)) return;

		var fileName = Path.GetFileName(realPath);
		var dstAsset = Path.Combine(target.Filepath, fileName);
		var dstMeta = dstAsset + ".meta";
		var srcMeta = realPath + ".meta";

		try {
			if (dstAsset == realPath) return; // same location
			File.Move(realPath, dstAsset, overwrite: false);
			if (File.Exists(srcMeta)) File.Move(srcMeta, dstMeta, overwrite: false);
		} catch { }

		AssetDatabase.RebuildAssetDatabase();
	}

	private static string UniqueDestPath(string path) {
		if (!File.Exists(path)) return path;
		var dir = Path.GetDirectoryName(path)!;
		var stem = Path.GetFileNameWithoutExtension(path);
		var ext = Path.GetExtension(path);
		var m = Regex.Match(stem, @"^(.*?)(\d+)$");
		string prefix;
		int next;
		if (m.Success) {
			prefix = m.Groups[1].Value;
			next = int.Parse(m.Groups[2].Value) + 1;
		} else {
			prefix = stem + " ";
			next = 2;
		}
		for (var i = next; ; i++) {
			var candidate = Path.Combine(dir, $"{prefix}{i}{ext}");
			if (!File.Exists(candidate)) return candidate;
		}
	}

	private static void CopyMetaWithNewUid(string srcMeta, string dstMeta) {
		var header = MetaFile.ReadHeader(srcMeta);
		if (header is null) return;
		MetaFile.Write(dstMeta[..^5], header with { Uid = UidGenerator.Generate() });
	}

	private IReadOnlyList<object> GetTargets(object? param) =>
		param is { } p ? new List<object> { p } : m_selectedItems.ToList();

	private static bool IsEditable(AssetFile file) =>
		ProjectContext.IsInitialized &&
		file.Filepath.StartsWith(ProjectContext.AssetsPath, StringComparison.OrdinalIgnoreCase);

	private static bool IsEditable(AssetFolder folder) =>
		ProjectContext.IsInitialized &&
		folder.Filepath.StartsWith(ProjectContext.AssetsPath, StringComparison.OrdinalIgnoreCase);

	private static Window? ActiveWindow() {
		if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime d)
			return d.Windows.FirstOrDefault(w => w.IsActive) ?? d.Windows.FirstOrDefault();
		return null;
	}

	private static AssetFolder? FindParentFolder(AssetFolder folder, AssetFile file) {
		if (folder.Files.Contains(file)) return folder;
		foreach (var sub in folder.SubFolders)
			if (FindParentFolder(sub, file) is { } found)
				return found;
		return null;
	}

	private void OnDatabaseReloaded() {
		Dispatcher.UIThread.Post(Refresh);
	}

	public AssetFolder? SelectedFolder {
		get => m_selectedFolder;
		set {
			m_selectedFolder = value;
			if (value is not null) ExpandToFolder(value);
			Notify();
			Notify(nameof(CurrentItems));
			Notify(nameof(BreadcrumbItems));
			Notify(nameof(ItemCount));
		}
	}

	public ObservableCollection<AssetFolder> Folders { get; } = [];

	public string SearchText {
		get => m_searchText;
		set {
			// save folder before entering search mode
			if (!string.IsNullOrWhiteSpace(value) && string.IsNullOrWhiteSpace(m_searchText))
				m_preSearchFolder = m_selectedFolder;

			m_searchText = value;

			// restore folder when search is cleared
			if (string.IsNullOrWhiteSpace(value) && m_preSearchFolder is not null) {
				m_selectedFolder = m_preSearchFolder;
				m_preSearchFolder = null;
				Notify(nameof(SelectedFolder));
				Notify(nameof(BreadcrumbItems));
			} else if (!string.IsNullOrWhiteSpace(value)) {
				m_selectedFolder = null;
				Notify(nameof(SelectedFolder));
				Notify(nameof(BreadcrumbItems));
			}

			Notify();
			Notify(nameof(CurrentItems));
			Notify(nameof(ItemCount));
		}
	}

	public bool FilterNode {
		get => m_filterNode;
		set {
			m_filterNode = value;
			Notify();
			Notify(nameof(FilterAll));
			Notify(nameof(CurrentItems));
			Notify(nameof(ItemCount));
		}
	}

	public bool FilterTexture {
		get => m_filterTexture;
		set {
			m_filterTexture = value;
			Notify();
			Notify(nameof(FilterAll));
			Notify(nameof(CurrentItems));
			Notify(nameof(ItemCount));
		}
	}

	public bool FilterModel {
		get => m_filterModel;
		set {
			m_filterModel = value;
			Notify();
			Notify(nameof(FilterAll));
			Notify(nameof(CurrentItems));
			Notify(nameof(ItemCount));
		}
	}

	public bool FilterMaterial {
		get => m_filterMaterial;
		set {
			m_filterMaterial = value;
			Notify();
			Notify(nameof(FilterAll));
			Notify(nameof(CurrentItems));
			Notify(nameof(ItemCount));
		}
	}

	public bool FilterShader {
		get => m_filterShader;
		set {
			m_filterShader = value;
			Notify();
			Notify(nameof(FilterAll));
			Notify(nameof(CurrentItems));
			Notify(nameof(ItemCount));
		}
	}

	public bool FilterScript {
		get => m_filterScript;
		set {
			m_filterScript = value;
			Notify();
			Notify(nameof(FilterAll));
			Notify(nameof(CurrentItems));
			Notify(nameof(ItemCount));
		}
	}

	public bool FilterUnknown {
		get => m_filterUnknown;
		set {
			m_filterUnknown = value;
			Notify();
			Notify(nameof(FilterAll));
			Notify(nameof(CurrentItems));
			Notify(nameof(ItemCount));
		}
	}

	// Tri-state: true = all, false = none, null = mixed
	public bool? FilterAll {
		get {
			var all = m_filterNode && m_filterTexture && m_filterModel && m_filterMaterial && m_filterShader &&
			          m_filterScript && m_filterUnknown;
			var none = !m_filterNode && !m_filterTexture && !m_filterModel && !m_filterMaterial && !m_filterShader &&
			           !m_filterScript && !m_filterUnknown;
			return all ? true : none ? false : null;
		}
		set {
			var v = value ?? true;
			m_filterNode = m_filterTexture = m_filterModel = m_filterMaterial =
				m_filterShader = m_filterScript = m_filterUnknown = v;
			NotifyAllFilters();
			Notify(nameof(CurrentItems));
			Notify(nameof(ItemCount));
		}
	}

	public IReadOnlyList<BreadcrumbItem> BreadcrumbItems {
		get {
			if (m_selectedFolder is null) return [];
			var chain = new List<AssetFolder>();
			for (var f = m_selectedFolder; f is not null; f = f.Parent)
				chain.Insert(0, f);
			return chain.Select((f, i) => new BreadcrumbItem(
				f.Name,
				() => SelectedFolder = f,
				i == chain.Count - 1
			)).ToList();
		}
	}

	public IEnumerable<object> CurrentItems {
		get {
			if (!string.IsNullOrWhiteSpace(m_searchText)) {
				var (textFilter, typeFilter) = ParseSearch(m_searchText);
				return Folders
					.SelectMany(GetAllFiles)
					.Where(f => IsTypeVisible(f.Type))
					.Where(f => typeFilter is null || f.Type == typeFilter)
					.Where(f => string.IsNullOrEmpty(textFilter) ||
					            f.Name.Contains(textFilter, StringComparison.OrdinalIgnoreCase));
			}

			if (m_selectedFolder is null) return [];
			var folders = m_selectedFolder.SubFolders.Cast<object>();
			var files = m_selectedFolder.Files
				.Where(f => IsTypeVisible(f.Type))
				.Cast<object>();
			return folders.Concat(files);
		}
	}

	public string ItemCount {
		get {
			var total = CurrentItems.Count();
			return $"{total} items ({m_selectedCount} selected)";
		}
	}

	public ICommand RefreshCommand { get; }
	public ICommand ExpandAllCommand { get; }
	public ICommand CollapseAllCommand { get; }
	public ICommand CreateFolderCommand { get; }
	public ICommand RenameCommand { get; }
	public ICommand DeleteCommand { get; }
	public ICommand CopyCommand { get; }
	public ICommand CutCommand { get; }
	public ICommand PasteCommand { get; }
	public ICommand DuplicateCommand { get; }
	public ICommand NewNodeCommand { get; }
	public ICommand NewNode3DCommand { get; }
	public ICommand NewNodeGenericCommand { get; }

	public new event PropertyChangedEventHandler? PropertyChanged;

	private void Notify([CallerMemberName] string? name = null) {
		PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
	}

	private void NotifyAllFilters() {
		Notify(nameof(FilterNode));
		Notify(nameof(FilterTexture));
		Notify(nameof(FilterModel));
		Notify(nameof(FilterMaterial));
		Notify(nameof(FilterShader));
		Notify(nameof(FilterScript));
		Notify(nameof(FilterUnknown));
		Notify(nameof(FilterAll));
	}

	private void SetAllExpanded(bool expanded) {
		foreach (var root in Folders)
			SetExpandedRecursive(root, expanded);
	}

	private static void SetExpandedRecursive(AssetFolder folder, bool expanded) {
		folder.IsExpanded = expanded;
		foreach (var sub in folder.SubFolders)
			SetExpandedRecursive(sub, expanded);
	}

	private static void ExpandToFolder(AssetFolder folder) {
		folder.IsExpanded = true;
		for (var f = folder.Parent; f is not null; f = f.Parent)
			f.IsExpanded = true;
	}

	private void LoadFolders() {
		m_selectedItems.Clear();

		// fallback paths for the designer (never reached at runtime)
		var assetsPath = ProjectContext.IsInitialized
			? ProjectContext.AssetsPath
			: @"C:\Users\Xein\Desktop\unnamed_project\assets";
		var corePath = ProjectContext.IsInitialized
			? ProjectContext.CorePath
			: @"C:\Users\Xein\code\toast-engine\out\Debug\toast_engine\bin\assets";

		Folders.Clear();

		var assetsFolder = new AssetFolder(assetsPath);
		assetsFolder.Name = "assets://";
		assetsFolder.IsExpanded = true;
		Folders.Add(assetsFolder);

		var coreFolder = new AssetFolder(corePath);
		coreFolder.Name = "core://";
		Folders.Add(coreFolder);

		SetOwnerRecursive(assetsFolder);
		SetOwnerRecursive(coreFolder);

		// restore or default to assets:// root
		AssetFolder? restored = null;
		if (m_refreshTargetPath is not null)
			restored = FindByPath(Folders, m_refreshTargetPath);
		m_refreshTargetPath = null;

		m_selectedFolder = restored ?? assetsFolder;
		ExpandToFolder(m_selectedFolder);

		Notify(nameof(SelectedFolder));
		Notify(nameof(CurrentItems));
		Notify(nameof(BreadcrumbItems));
		Notify(nameof(ItemCount));
	}

	private void Refresh() {
		m_refreshTargetPath = m_selectedFolder?.Filepath;
		LoadFolders();
	}

	private void SetOwnerRecursive(AssetFolder folder) {
		folder.Owner = this;
		foreach (var file in folder.Files) file.Owner = this;
		foreach (var sub in folder.SubFolders) SetOwnerRecursive(sub);
	}

	private static AssetFolder? FindByPath(IEnumerable<AssetFolder> folders, string path) {
		foreach (var f in folders) {
			if (string.Equals(f.Filepath, path, StringComparison.OrdinalIgnoreCase)) return f;
			var found = FindByPath(f.SubFolders, path);
			if (found is not null) return found;
		}
		return null;
	}

	private static IEnumerable<AssetFile> GetAllFiles(AssetFolder folder) {
		foreach (var file in folder.Files)
			yield return file;
		foreach (var sub in folder.SubFolders)
		foreach (var file in GetAllFiles(sub))
			yield return file;
	}

	// filters by type and name at the same time
	private static (string text, FileType? type) ParseSearch(string query) {
		var match = Regex.Match(query, @"Type=(\w+)", RegexOptions.IgnoreCase);
		FileType? typeFilter = null;
		var text = query;
		if (match.Success) {
			if (Enum.TryParse<FileType>(match.Groups[1].Value, true, out var ft))
				typeFilter = ft;
			text = query.Replace(match.Value, "").Trim();
		}

		return (text, typeFilter);
	}

	private bool IsTypeVisible(FileType type) {
		return type switch {
			FileType.Node => m_filterNode,
			FileType.Texture => m_filterTexture,
			FileType.Model => m_filterModel,
			FileType.Material => m_filterMaterial,
			FileType.Shader => m_filterShader,
			FileType.Script => m_filterScript,
			_ => m_filterUnknown
		};
	}
}
