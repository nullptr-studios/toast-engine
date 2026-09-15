using System;
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
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets.Importers;
using editor.Assets.Types;
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

public class AssetBrowserViewModel : Tool, INotifyPropertyChanged, IDisposable {
	// Extension sets derived dynamically from importers + asset registry
	// TODO: Do this with reflection at some point
	private static readonly IReadOnlyList<IAssetImporter> s_defaultImporters = [
		new TextureImporter(new TextureImporter.Settings()),
		new PsdImporter(new TextureImporter.Settings(), new PsdImporter.Settings()),
		new GltfImporter(new GltfImporter.Settings(), new TextureImporter.Settings()),
		new FontImporter(),
		new UIImageImporter()
	];

	private static readonly HashSet<string> s_artworkExts = new(
		s_defaultImporters.SelectMany(i => i.SupportedExtensions),
		StringComparer.OrdinalIgnoreCase);

	private static readonly HashSet<string> s_assetExts = new(
		AssetTypeRegistry.All.Select(a => a.Extension),
		StringComparer.OrdinalIgnoreCase);

	private static readonly HashSet<string> s_reservedNames = ["root", "world", "global"];
	private readonly List<IRelayCommand> m_actionCommands = [];

	// selection
	private readonly HashSet<object> m_selectedItems = [];

	private readonly AssetTypeFilter m_unknownFilter;
	private ClipMode m_clipMode;
	private List<string> m_clipPaths = []; // real paths
	private string? m_preSearchFolderPath;
	private string? m_refreshTargetPath;
	private string m_searchText = "";
	private int m_selectedCount;
	private AssetFolder? m_selectedFolder;

	public AssetBrowserViewModel() {
		Current = this;

		m_unknownFilter = new AssetTypeFilter(null);
		var filters = AssetTypeRegistry.All
			.Where(a => a is not ProjectSettingsAsset)
			.Select(a => new AssetTypeFilter(a))
			.Append(m_unknownFilter)
			.OrderBy(f => f.Label, StringComparer.OrdinalIgnoreCase)
			.ThenBy(f => f.Label, StringComparer.Ordinal)
			.ToList();
		Filters = new ObservableCollection<AssetTypeFilter>(filters);
		RootFilters = new ObservableCollection<AssetTypeFilter>(filters.Where(f =>
			string.IsNullOrWhiteSpace(f.Definition?.Category)));
		FilterGroups = new ObservableCollection<AssetTypeFilterGroup>(filters
			.Where(f => !string.IsNullOrWhiteSpace(f.Definition?.Category))
			.GroupBy(f => f.Definition!.Category)
			.OrderBy(g => g.Key, StringComparer.OrdinalIgnoreCase)
			.ThenBy(g => g.Key, StringComparer.Ordinal)
			.Select(g => new AssetTypeFilterGroup(g.Key, g)));
		FilterTreeItems = new ObservableCollection<object>(
			FilterGroups.Cast<object>().Concat(RootFilters));
		foreach (var f in Filters)
			f.PropertyChanged += OnFilterChanged;

		RefreshCommand = new RelayCommand(Refresh);
		ExpandAllCommand = new RelayCommand(() => SetAllExpanded(true));
		CollapseAllCommand = new RelayCommand(() => SetAllExpanded(false));
		CreateFolderCommand = Track(new AsyncRelayCommand<object>(CreateFolder, CanCreateFolder));
		RenameCommand = Track(new AsyncRelayCommand<object>(RenameAsync, CanRename));
		DeleteCommand = Track(new AsyncRelayCommand<object>(DeleteAsync, CanDelete));
		CopyCommand = Track(new RelayCommand<object>(Copy, CanCopy));
		CutCommand = Track(new RelayCommand<object>(Cut, CanCut));
		PasteCommand = Track(new RelayCommand(Paste, CanPaste));
		DuplicateCommand = Track(new AsyncRelayCommand<object>(DuplicateAsync, CanDuplicate));
		NewNodeCommand =
			Track(new AsyncRelayCommand(() => CreatePrefab("Node", "toast::Node"), () => CanWriteToSelectedFolder));
		NewNode3DCommand =
			Track(new AsyncRelayCommand(() => CreatePrefab("Node3D", "toast::Node3D"), () => CanWriteToSelectedFolder));
		NewNodeGenericCommand = Track(new AsyncRelayCommand(CreateGenericPrefab, () => CanWriteToSelectedFolder));
		NewAssetCommand = Track(new AsyncRelayCommand<object>(o => CreateNewAsset(o as BaseAsset),
			o => o is BaseAsset && CanWriteToSelectedFolder));
		ReimportCommand = Track(new AsyncRelayCommand<object>(ReimportAsync, CanReimport));
		LoadFolders();

		// auto-reload whenever the asset database changes
		AssetDatabase.ReloadedDatabase += OnDatabaseReloaded;
	}

	public static AssetBrowserViewModel? Current { get; private set; }

	public IReadOnlySet<object> SelectedItems => m_selectedItems;

	public AssetFolder? SelectedFolder {
		get => m_selectedFolder;
		set {
			if (value is null) {
				value = FindFallbackFolder();
				if (value is null) return;
				if (ReferenceEquals(m_selectedFolder, value)) {
					Notify();
					return;
				}
			}

			if (ReferenceEquals(m_selectedFolder, value)) return;
			ClearSelection();
			m_selectedFolder = value;
			if (value is not null) ExpandToFolder(value);
			Notify();
			RefreshCurrentItems();
			Notify(nameof(BreadcrumbItems));
			Notify(nameof(CanWriteToSelectedFolder));
			NotifyActionStateChanged();
		}
	}

	public ObservableCollection<AssetFolder> Folders { get; } = [];

	public ObservableCollection<AssetTypeFilter> Filters { get; }
	public ObservableCollection<AssetTypeFilter> RootFilters { get; }
	public ObservableCollection<AssetTypeFilterGroup> FilterGroups { get; }
	public ObservableCollection<object> FilterTreeItems { get; }
	public bool CanWriteToSelectedFolder => CanWriteToFolder(m_selectedFolder);

	public string SearchText {
		get => m_searchText;
		set {
			if (!string.IsNullOrWhiteSpace(value) && string.IsNullOrWhiteSpace(m_searchText))
				m_preSearchFolderPath = m_selectedFolder?.Filepath;

			m_searchText = value;

			if (string.IsNullOrWhiteSpace(value) && m_preSearchFolderPath is not null) {
				m_selectedFolder = FindByPath(Folders, m_preSearchFolderPath) ?? FindFallbackFolder();
				m_preSearchFolderPath = null;
				if (m_selectedFolder is not null) ExpandToFolder(m_selectedFolder);
				Notify(nameof(SelectedFolder));
				Notify(nameof(BreadcrumbItems));
			}

			Notify();
			RefreshCurrentItems();
			Notify(nameof(CanWriteToSelectedFolder));
			NotifyActionStateChanged();
		}
	}

	// Tri-state: true = all, false = none, null = mixed
	public bool? FilterAll {
		get {
			var all = Filters.All(f => f.IsEnabled);
			var none = Filters.All(f => !f.IsEnabled);
			return all ? true : none ? false : null;
		}
		set {
			var v = value ?? FilterAll != true;
			foreach (var f in Filters)
				f.IsEnabled = v;
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

	public ObservableCollection<object> CurrentItems { get; } = [];

	public string ItemCount => $"{CurrentItems.Count} items ({m_selectedCount} selected)";

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
	public ICommand NewAssetCommand { get; }
	public ICommand ReimportCommand { get; }

	public void Dispose() {
		AssetDatabase.ReloadedDatabase -= OnDatabaseReloaded;
		foreach (var filter in Filters) filter.PropertyChanged -= OnFilterChanged;
		if (ReferenceEquals(Current, this)) Current = null;
		GC.SuppressFinalize(this);
	}

	public new event PropertyChangedEventHandler? PropertyChanged;

	public void RevealAsset(string uid) {
		foreach (var root in Folders)
		foreach (var file in GetAllFiles(root)) {
			if (file.Uid != uid) continue;
			SearchText = "";
			SelectedFolder = FindParentFolder(root, file);
			return;
		}
	}

	public void SelectItem(object item, KeyModifiers modifiers) {
		if (modifiers.HasFlag(KeyModifiers.Control)) {
			if (m_selectedItems.Remove(item)) {
				SetIsSelected(item, false);
			} else {
				m_selectedItems.Add(item);
				SetIsSelected(item, true);
			}
		} else {
			foreach (var prev in m_selectedItems)
				SetIsSelected(prev, false);
			m_selectedItems.Clear();
			m_selectedItems.Add(item);
			SetIsSelected(item, true);
		}

		m_selectedCount = m_selectedItems.Count;
		Notify(nameof(ItemCount));
		NotifyActionStateChanged();
	}

	public void ClearSelection() {
		foreach (var item in m_selectedItems)
			SetIsSelected(item, false);
		m_selectedItems.Clear();
		m_selectedCount = 0;
		Notify(nameof(ItemCount));
		NotifyActionStateChanged();
	}

	private static void SetIsSelected(object item, bool selected) {
		switch (item) {
			case AssetFile f: f.IsSelected = selected; break;
			case AssetFolder d: d.IsSelected = selected; break;
		}
	}

	private async Task CreateFolder(object? param) {
		var parent = param as AssetFolder ?? m_selectedFolder;
		if (!CanWriteToFolder(parent)) return;
		var window = ActiveWindow();
		if (window is null) return;
		var name = await new NewFolderModal().ShowDialog<string?>(window);
		if (string.IsNullOrEmpty(name)) return;
		var parentPath = parent!.Filepath;
		Directory.CreateDirectory(Path.Combine(parentPath, name));
		m_refreshTargetPath = parentPath;
		Refresh();
	}

	private async Task ReimportAsync(object? param) {
		var file = param as AssetFile ?? m_selectedItems.OfType<AssetFile>().FirstOrDefault();
		if (file is null || !IsEditable(file)) return;

		var header = MetaFile.ReadHeader(file.Filepath);
		if (header?.Source is not { } sourceVirtual) {
			await App.Modals.ShowError("Cannot reimport",
				"This asset has no import source — it was not imported from an artwork file.");
			return;
		}

		var task = LoaderTask.DoWithProgress(
			$"Reimport {file.Name}",
			(log, progress) => AssetDatabase.Reimport(sourceVirtual, log, progress));

		var vm = new LoaderViewModel([task]) {
			OnComplete = async () => {
				AssetDatabase.RebuildAssetDatabase();
				ProjectContext.RaiseAssetsChanged();
				await Task.CompletedTask;
			}
		};

		var owner = ActiveWindow();
		if (owner is null) return;
		await new SimpleLoaderWindow(vm).ShowDialog(owner);
	}

	public async Task HandleDroppedFilesAsync(IReadOnlyList<string> paths) {
		if (!ProjectContext.IsInitialized || !CanWriteToSelectedFolder) return;

		var artworkFiles = new List<string>();
		var assetFiles = new List<string>();

		foreach (var path in paths) {
			var ext = Path.GetExtension(path).ToLowerInvariant();
			if (s_artworkExts.Contains(ext))
				artworkFiles.Add(path);
			else if (s_assetExts.Contains(ext))
				assetFiles.Add(path);
			// unknown: ignore
		}

		// Copy pre-built assets directly (no importer, just copy + meta)
		if (assetFiles.Count > 0) {
			var destDir = m_selectedFolder!.Filepath;
			foreach (var src in assetFiles) {
				var ext = Path.GetExtension(src).ToLowerInvariant();
				var definition = AssetTypeRegistry.ByExtension(ext);
				if (definition is null) continue;
				var dest = UniqueDestPath(Path.Combine(destDir, Path.GetFileName(src)));
				try {
					File.Copy(src, dest, false);
					MetaFile.Write(dest, new MetaHeader { Uid = UidGenerator.Generate(), Type = definition.Type });
				} catch {
					/* skip unreadable files */
				}
			}

			AssetDatabase.RebuildAssetDatabase();
			Refresh();
		}

		// Open compact import window for artwork files
		if (artworkFiles.Count > 0) {
			var owner = ActiveWindow();
			if (owner is null) return;

			var destDir = m_selectedFolder!.Filepath;
			var destVirtual = ProjectContext.ToVirtual(destDir) ?? "assets://";
			var vm = new CompactImportWindowViewModel(artworkFiles, destVirtual);
			await new CompactImportWindow(vm).ShowDialog(owner);
			Refresh();
		}
	}

	private async Task RenameAsync(object? param) {
		if (!CanRename(param)) return;
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
		var stem = Path.GetFileNameWithoutExtension(newName);
		if (s_reservedNames.Contains(stem)) {
			await App.Modals.ShowWarning("Reserved Name",
				$"'{stem}' is a reserved keyword and cannot be used as an asset name.");
			return;
		}

		var metaPath = file.Filepath;
		var oldAssetPath = metaPath[..^5];
		var assetExt = AssetTypeRegistry.GetExtension(Path.GetFileNameWithoutExtension(oldAssetPath));
		var dir = Path.GetDirectoryName(oldAssetPath)!;
		var newAssetPath = Path.Combine(dir, newName + assetExt);
		var newMetaPath = newAssetPath + ".meta";

		try {
			File.Move(oldAssetPath, newAssetPath, false);
			File.Move(metaPath, newMetaPath, false);
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
		if (s_reservedNames.Contains(newName)) {
			await App.Modals.ShowWarning("Reserved Name",
				$"'{newName}' is a reserved keyword and cannot be used as a folder name.");
			return;
		}

		var dir = Path.GetDirectoryName(folder.Filepath)!;
		var newPath = Path.Combine(dir, newName);

		try {
			Directory.Move(folder.Filepath, newPath);
		} catch (Exception ex) {
			await App.Modals.ShowError("Rename failed", ex.Message);
			return;
		}

		m_refreshTargetPath = newPath;
		AssetDatabase.RebuildAssetDatabase();
	}

	private async Task DeleteAsync(object? param) {
		if (!CanDelete(param)) return;
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
			"Delete",
			OkIcon: LucideIconKind.Shredder
		)).ShowDialog<bool?>(window) == true;
		if (!confirmed) return;

		foreach (var t in targets)
			switch (t) {
				case AssetFile file when IsEditable(file):
					if (file.Uid is { } uid)
						AssetDatabase.RemoveArtworkOutputs(uid);
					var assetPath = file.Filepath[..^5];
					AssetDatabase.TryDelete(assetPath);
					AssetDatabase.TryDelete(file.Filepath);
					break;
				case AssetFolder folder when IsEditable(folder):
					try {
						Directory.Delete(folder.Filepath, true);
					} catch {
						/* ignore */
					}

					break;
			}

		AssetDatabase.RebuildAssetDatabase();
	}

	private void Copy(object? param) {
		if (!CanCopy(param)) return;
		var items = GetTargets(param);
		m_clipPaths = items.OfType<AssetFile>().Select(f => f.Filepath[..^5]).ToList();
		m_clipMode = ClipMode.Copy;
		NotifyActionStateChanged();
	}

	private void Cut(object? param) {
		if (!CanCut(param)) return;
		var items = GetTargets(param);
		m_clipPaths = items.OfType<AssetFile>().Select(f => f.Filepath[..^5]).ToList();
		m_clipMode = ClipMode.Cut;
		NotifyActionStateChanged();
	}

	private void Paste() {
		if (!CanPaste()) return;
		var dest = m_selectedFolder!.Filepath;

		foreach (var src in m_clipPaths) {
			if (!File.Exists(src)) continue;
			var srcMeta = src + ".meta";
			var fileName = Path.GetFileName(src);
			var dstAsset = Path.Combine(dest, fileName);
			var dstMeta = dstAsset + ".meta";

			if (m_clipMode == ClipMode.Cut) {
				// move, keep UID
				try {
					File.Move(src, dstAsset, false);
					if (File.Exists(srcMeta)) File.Move(srcMeta, dstMeta, false);
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
		NotifyActionStateChanged();
	}

	private async Task DuplicateAsync(object? param) {
		if (!CanDuplicate(param)) return;
		var files = GetTargets(param).OfType<AssetFile>().ToList();
		if (files.Count == 0) return;
		foreach (var file in files) {
			if (!IsEditable(file)) continue;
			var src = file.Filepath[..^5];
			var srcMeta = file.Filepath;
			var dir = Path.GetDirectoryName(src)!;
			var filename = Path.GetFileName(src);
			var ext = AssetTypeRegistry.GetExtension(filename);
			var stem = filename[..^ext.Length];
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
		if (!CanWriteToSelectedFolder) return;
		var window = ActiveWindow();
		if (window is null) return;
		var popup = new NodeTypeTree();
		var type = await popup.ShowDialog<string?>(window);
		if (type is null) return;

		var defaultName = type[(type.LastIndexOf(':') + 1)..];
		await CreatePrefab(defaultName, type);
	}

	private async Task CreatePrefab(string defaultName, string nodeType) {
		if (!CanWriteToSelectedFolder) return;
		var window = ActiveWindow();
		if (window is null) return;
		var name = await new RenameModal(defaultName).ShowDialog<string?>(window);
		if (string.IsNullOrEmpty(name)) return;

		var dest = m_selectedFolder!.Filepath;
		var path = UniqueDestPath(Path.Combine(dest, name + ".tnode"));
		try {
			ToastEngine.CreateTNode(path, nodeType);
			MetaFile.Write(path,
				new MetaHeader { Uid = UidGenerator.Generate(), Type = AssetTypeRegistry.ByExtension(".tnode")!.Type });
		} catch (Exception ex) {
			await App.Modals.ShowError("Create failed", ex.Message);
			return;
		}

		AssetDatabase.RebuildAssetDatabase();
	}

	private async Task CreateNewAsset(BaseAsset? def) {
		if (def is null || !CanWriteToSelectedFolder) return;
		var window = ActiveWindow();
		if (window is null) return;
		var name = await new RenameModal("New" + def.DisplayName).ShowDialog<string?>(window);
		if (string.IsNullOrEmpty(name)) return;

		var dest = m_selectedFolder!.Filepath;
		var path = UniqueDestPath(Path.Combine(dest, name + def.Extension));
		try {
			await def.CreateAsync(path);
			MetaFile.Write(path, new MetaHeader { Uid = UidGenerator.Generate(), Type = def.Type });
		} catch (Exception ex) {
			await App.Modals.ShowError("Create failed", ex.Message);
			return;
		}

		AssetDatabase.RebuildAssetDatabase();
	}

	public void MoveAsset(string uid, AssetFolder target) {
		if (!CanMoveAsset(uid, target)) return;
		if (!AssetDatabase.TryResolve(uid, out var virtualPath, out _)) return;
		var realPath = ProjectContext.Resolve(virtualPath);
		if (!File.Exists(realPath)) return;

		var fileName = Path.GetFileName(realPath);
		var dstAsset = Path.Combine(target.Filepath, fileName);
		var dstMeta = dstAsset + ".meta";
		var srcMeta = realPath + ".meta";

		try {
			if (dstAsset == realPath) return;
			File.Move(realPath, dstAsset, false);
			if (File.Exists(srcMeta)) File.Move(srcMeta, dstMeta, false);
		} catch { }

		AssetDatabase.RebuildAssetDatabase();
	}

	private static string UniqueDestPath(string path) {
		if (!File.Exists(path)) return path;
		var dir = Path.GetDirectoryName(path)!;
		var name = Path.GetFileName(path);
		var ext = AssetTypeRegistry.GetExtension(name);
		var stem = name[..^ext.Length];
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

		for (var i = next;; i++) {
			var candidate = Path.Combine(dir, $"{prefix}{i}{ext}");
			if (!File.Exists(candidate)) return candidate;
		}
	}

	private static void CopyMetaWithNewUid(string srcMeta, string dstMeta) {
		var header = MetaFile.ReadHeader(srcMeta);
		if (header is null) return;
		MetaFile.Write(dstMeta[..^5], header with { Uid = UidGenerator.Generate() });
	}

	private IReadOnlyList<object> GetTargets(object? param) {
		return param is { } p ? new List<object> { p } : m_selectedItems.ToList();
	}

	public bool CanWriteToFolder(AssetFolder? folder) {
		if (folder is null || !ProjectContext.IsInitialized || ProjectContext.IsUnderCore(folder.Filepath)) return false;
		return ProjectContext.IsDatabaseRoot(folder.Filepath) ||
			ProjectContext.IsUnderContentDatabase(folder.Filepath);
	}

	public bool CanOpenForEditing(AssetFile file) {
		return file.Definition?.CanBeEdited == true && !ProjectContext.IsUnderCore(file.Filepath);
	}

	public bool CanMoveAsset(string uid, AssetFolder target) {
		if (!CanWriteToFolder(target) || !AssetDatabase.TryResolve(uid, out var virtualPath, out _)) return false;
		var source = ProjectContext.Resolve(virtualPath);
		return File.Exists(source) && IsEditablePath(source);
	}

	private bool CanCreateFolder(object? param) {
		return CanWriteToFolder(param as AssetFolder ?? m_selectedFolder);
	}

	private bool CanRename(object? param) {
		var targets = GetTargets(param);
		return targets.Count == 1 && targets[0] switch {
			AssetFile file => IsEditable(file),
			AssetFolder folder => IsEditable(folder),
			_ => false
		};
	}

	private bool CanDelete(object? param) {
		var targets = GetTargets(param);
		return targets.Count > 0 && targets.All(target => target switch {
			AssetFile file => IsEditable(file),
			AssetFolder folder => IsEditable(folder),
			_ => false
		});
	}

	private bool CanCopy(object? param) {
		var targets = GetTargets(param);
		return targets.Count > 0 && targets.All(target => target is AssetFile);
	}

	private bool CanCut(object? param) {
		var targets = GetTargets(param);
		return targets.Count > 0 && targets.All(target => target is AssetFile file && IsEditable(file));
	}

	private bool CanPaste() {
		return m_clipMode != ClipMode.None && m_clipPaths.Count > 0 && CanWriteToSelectedFolder;
	}

	private bool CanDuplicate(object? param) {
		var targets = GetTargets(param);
		return targets.Count > 0 && targets.All(target => target is AssetFile file && IsEditable(file));
	}

	private bool CanReimport(object? param) {
		var targets = GetTargets(param);
		return targets.Count == 1 && targets[0] is AssetFile file && IsEditable(file);
	}

	private static bool IsEditablePath(string path) {
		return ProjectContext.IsInitialized &&
			!ProjectContext.IsUnderCore(path) &&
			ProjectContext.IsUnderContentDatabase(path);
	}

	private static bool IsEditable(AssetFile file) {
		return IsEditablePath(file.Filepath);
	}

	private static bool IsEditable(AssetFolder folder) {
		return IsEditablePath(folder.Filepath) && !ProjectContext.IsDatabaseRoot(folder.Filepath);
	}

	private T Track<T>(T command) where T : IRelayCommand {
		m_actionCommands.Add(command);
		return command;
	}

	private void NotifyActionStateChanged() {
		foreach (var command in m_actionCommands)
			command.NotifyCanExecuteChanged();
	}

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

	private void OnFilterChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName != nameof(AssetTypeFilter.IsEnabled)) return;
		Notify(nameof(FilterAll));
		RefreshCurrentItems();
	}

	private void RefreshCurrentItems() {
		IEnumerable<object> items;
		if (!string.IsNullOrWhiteSpace(m_searchText)) {
			var (textFilter, typeFilter) = ParseSearch(m_searchText);
			items = Folders
				.SelectMany(GetAllFiles)
				.Where(f => IsTypeVisible(f.Definition))
				.Where(f => typeFilter is null || f.Definition == typeFilter)
				.Where(f => string.IsNullOrEmpty(textFilter) ||
					f.Name.Contains(textFilter, StringComparison.OrdinalIgnoreCase))
				.OrderBy(f => f.Name, StringComparer.OrdinalIgnoreCase)
				.ThenBy(f => f.Name, StringComparer.Ordinal);
		} else if (m_selectedFolder is not null) {
			var folders = m_selectedFolder.SubFolders
				.OrderBy(f => f.Name, StringComparer.OrdinalIgnoreCase)
				.ThenBy(f => f.Name, StringComparer.Ordinal)
				.Cast<object>();
			var files = m_selectedFolder.Files
				.Where(f => IsTypeVisible(f.Definition))
				.OrderBy(f => f.Name, StringComparer.OrdinalIgnoreCase)
				.ThenBy(f => f.Name, StringComparer.Ordinal)
				.Cast<object>();
			items = folders.Concat(files);
		} else {
			items = [];
		}

		CurrentItems.Clear();
		foreach (var item in items)
			CurrentItems.Add(item);

		Notify(nameof(ItemCount));
	}

	private void Notify([CallerMemberName] string? name = null) {
		PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
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
		m_selectedCount = 0;
		Folders.Clear();
		var roots = new List<AssetFolder>();

		if (ProjectContext.IsInitialized) {
			// One folder per content database
			foreach (var db in ProjectContext.Databases) {
				var dbPath = Path.Combine(ProjectContext.ProjectPath, db);
				roots.Add(new AssetFolder(dbPath) { Name = db + "://" });
			}

			// core:// is always appended
			roots.Add(new AssetFolder(ProjectContext.CorePath) { Name = "core://" });
		} else {
			// show a minimal placeholder
			var fallbackFolder = new AssetFolder(@"C:\Users\Xein\Desktop\unnamed_project\assets") {
				Name = "assets://", IsExpanded = true
			};
			roots.Add(fallbackFolder);
		}

		foreach (var root in roots
			         .OrderBy(f => f.Name, StringComparer.OrdinalIgnoreCase)
			         .ThenBy(f => f.Name, StringComparer.Ordinal)) {
			Folders.Add(root);
			SetOwnerRecursive(root);
		}

		AssetFolder? restored = null;
		if (m_refreshTargetPath is not null)
			restored = FindByPath(Folders, m_refreshTargetPath);
		m_refreshTargetPath = null;

		m_selectedFolder = restored ?? FindFallbackFolder();
		if (m_selectedFolder is not null) ExpandToFolder(m_selectedFolder);

		Notify(nameof(SelectedFolder));
		RefreshCurrentItems();
		Notify(nameof(BreadcrumbItems));
		Notify(nameof(CanWriteToSelectedFolder));
		NotifyActionStateChanged();
	}

	private void Refresh() {
		m_refreshTargetPath ??= m_selectedFolder?.Filepath;
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

	private AssetFolder? FindFallbackFolder() {
		if (ProjectContext.IsInitialized) {
			var assets = FindByPath(Folders, ProjectContext.AssetsPath);
			if (assets is not null) return assets;
		}

		return Folders.FirstOrDefault(CanWriteToFolder) ?? Folders.FirstOrDefault();
	}

	private static IEnumerable<AssetFile> GetAllFiles(AssetFolder folder) {
		foreach (var file in folder.Files)
			yield return file;
		foreach (var sub in folder.SubFolders)
		foreach (var file in GetAllFiles(sub))
			yield return file;
	}

	private static (string text, BaseAsset? type) ParseSearch(string query) {
		var match = Regex.Match(query, @"Type=(\w+)", RegexOptions.IgnoreCase);
		BaseAsset? typeFilter = null;
		var text = query;
		if (match.Success) {
			typeFilter = AssetTypeRegistry.All
				.FirstOrDefault(a => a.DisplayName.Equals(match.Groups[1].Value, StringComparison.OrdinalIgnoreCase));
			text = query.Replace(match.Value, "").Trim();
		}

		return (text, typeFilter);
	}

	private bool IsTypeVisible(BaseAsset? def) {
		if (def is null) return m_unknownFilter.IsEnabled;
		return Filters.FirstOrDefault(f => f.Definition == def)?.IsEnabled ?? true;
	}

	// clipboard
	private enum ClipMode { None, Copy, Cut }
}
