using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.Components.Elements;
using editor.Components.Modals;

namespace editor.Assets.Importers;

public abstract partial class ImportNodeViewModel : ViewModelBase {
	[ObservableProperty] private string m_fullPath = string.Empty;
	[ObservableProperty] private bool m_isEnabled = true;
	[ObservableProperty] private bool m_isExpanded = true;
	[ObservableProperty] private bool m_isReadOnly;
	[ObservableProperty] private bool m_isVisible = true;
	[ObservableProperty] private string m_name = string.Empty;
	[ObservableProperty] private IReadOnlyList<TextSegment> m_nameSegments = [];

	private bool? m_isSelected = false;

	protected ImportNodeViewModel(string path, ImportFolderViewModel? parent) {
		FullPath = Path.GetFullPath(path);
		Name = Path.GetFileName(path);
		Parent = parent;
		m_nameSegments = [new TextSegment(Name, false)];
	}

	// tri-state: true = selected, false = deselected, null = mixed
	public bool? IsSelected {
		get => m_isSelected;
		set {
			if (SetProperty(ref m_isSelected, value)) OnSelectionChanged(value);
		}
	}

	public ImportFolderViewModel? Parent { get; }

	public virtual bool ShowChip => false;
	public virtual string ChipText => string.Empty;
	public virtual string ChipColor => string.Empty;

	public virtual bool IsImportedFile => false;

	public virtual bool ShowImportAccent => false;
	public virtual bool ShowReimportAccent => false;

	public virtual Action? SelectionChangedCallback { get; set; }

	protected virtual void OnSelectionChanged(bool? value) { }

	public virtual void ToggleSelection() {
		if (!IsEnabled || IsReadOnly) return;
		IsSelected = IsSelected != true;
	}

	// Sets the backing field without firing OnSelectionChanged (used when bubbling up)
	public void UpdateSelectionSilently(bool? value) =>
		SetProperty(ref m_isSelected, value, nameof(IsSelected));

	public abstract void ApplyFilter(string filter);

	protected void BuildNameSegments(string filter) {
		if (string.IsNullOrEmpty(filter)) {
			NameSegments = [new TextSegment(Name, false)];
			return;
		}

		var idx = Name.IndexOf(filter, StringComparison.OrdinalIgnoreCase);
		if (idx < 0) {
			NameSegments = [new TextSegment(Name, false)];
			return;
		}

		var segs = new List<TextSegment>();
		if (idx > 0) segs.Add(new TextSegment(Name[..idx], false));
		segs.Add(new TextSegment(Name[idx..(idx + filter.Length)], true));
		if (idx + filter.Length < Name.Length)
			segs.Add(new TextSegment(Name[(idx + filter.Length)..], false));
		NameSegments = segs;
	}
}

public class ImportFileViewModel : ImportNodeViewModel {
	private readonly string m_chipText;
	private readonly string m_chipColor;

	public ImportFileViewModel(
		string path, ImportFolderViewModel? parent,
		List<string> allowedExtensions, HashSet<string> importedFiles,
		List<IAssetImporter> importers, ImportTreeState state)
		: base(path, parent) {
		var ext = Path.GetExtension(path).ToLowerInvariant();

		if (importedFiles.Contains(path) || importedFiles.Contains(FullPath)) {
			// Already imported
			IsImported = true;
			IsSelected = false;
		} else if (!allowedExtensions.Contains(ext)) {
			// Unsupported extension
			IsEnabled = false;
			IsSelected = false;
		} else {
			// Normal file
			var saved = state.GetFileSelected(FullPath);
			IsSelected = saved ?? false;
		}

		// Resolve chip info from the matching importer's primary output type
		var matchedImporter = importers.FirstOrDefault(i => i.SupportedExtensions.Contains(ext));
		if (matchedImporter != null) {
			var outputType = matchedImporter.PrimaryOutputType;
			m_chipText = outputType.ChipText;
			m_chipColor = outputType.ChipColor;
		} else {
			m_chipText = string.Empty;
			m_chipColor = string.Empty;
		}
	}

	/// True when this file was previously imported
	public bool IsImported { get; }

	public override bool IsImportedFile => IsImported;
	public override bool ShowImportAccent => IsSelected == true && !IsImported;
	public override bool ShowReimportAccent => IsSelected == true && IsImported;

	public override bool ShowChip => !string.IsNullOrEmpty(m_chipText);
	public override string ChipText => m_chipText;
	public override string ChipColor => m_chipColor;

	protected override void OnSelectionChanged(bool? value) {
		Parent?.UpdateSelectionFromChildren();
		OnPropertyChanged(nameof(ShowImportAccent));
		OnPropertyChanged(nameof(ShowReimportAccent));
		SelectionChangedCallback?.Invoke();
	}

	public void ApplyFilter(string filter, bool showImported) {
		if (IsImported) {
			IsVisible = showImported && (string.IsNullOrWhiteSpace(filter) ||
			                             Name.Contains(filter, StringComparison.OrdinalIgnoreCase));
		} else {
			IsVisible = string.IsNullOrWhiteSpace(filter) ||
			            Name.Contains(filter, StringComparison.OrdinalIgnoreCase);
		}
		BuildNameSegments(filter);
	}

	public override void ApplyFilter(string filter) => ApplyFilter(filter, false);

	private Action? m_selectionCallback;
	public override Action? SelectionChangedCallback {
		get => m_selectionCallback;
		set => m_selectionCallback = value;
	}
}

public class ImportFolderViewModel : ImportNodeViewModel {
	private ImportTreeState? m_state;

	public ImportFolderViewModel(
		string path, ImportFolderViewModel? parent,
		List<string> allowedExtensions, HashSet<string> importedFiles,
		List<IAssetImporter> importers, ImportTreeState state)
		: base(path, parent) {
		m_state = state;

		foreach (var dir in Directory.EnumerateDirectories(path))
			Children.Add(new ImportFolderViewModel(dir, this, allowedExtensions, importedFiles, importers, state));
		foreach (var file in Directory.EnumerateFiles(path))
			Children.Add(new ImportFileViewModel(file, this, allowedExtensions, importedFiles, importers, state));

		// Restore expand state
		IsExpanded = !state.GetFolderCollapsed(FullPath);

		UpdateSelectionFromChildren();

		// Persist collapse state whenever IsExpanded changes
		PropertyChanged += (_, e) => {
			if (e.PropertyName == nameof(IsExpanded))
				m_state?.SetFolderCollapsed(FullPath, !IsExpanded);
		};
	}

	public ObservableCollection<ImportNodeViewModel> Children { get; } = [];

	public override void ToggleSelection() {
		if (!IsEnabled) return;
		var selectable = SelectableChildren().ToList();
		if (selectable.Count == 0) return;
		bool allSelected = selectable.All(c => c.IsSelected == true);
		// Propagate via IsSelected setter
		IsSelected = !allSelected;
	}

	protected override void OnSelectionChanged(bool? value) {
		if (value == null) return;
		// Propagate down only to non-imported, non-readonly, enabled children
		foreach (var child in SelectableChildren())
			child.IsSelected = value;
		Parent?.UpdateSelectionFromChildren();
	}

	/// Recomputes this folder's tri-state from its children and bubbles up
	public void UpdateSelectionFromChildren() {
		var importedCount = Children.Count(c => c is ImportFileViewModel { IsImported: true });
		var selectableChildren = SelectableChildren().ToList();
		bool hasSelectable = selectableChildren.Count > 0;
		bool hasImported = importedCount > 0;

		if (!hasSelectable && !hasImported) {
			// Empty folder
			IsEnabled = false;
			UpdateSelectionSilently(false);
			Parent?.UpdateSelectionFromChildren();
			return;
		}

		if (!hasSelectable) {
			IsEnabled = false;
			UpdateSelectionSilently(false);
			Parent?.UpdateSelectionFromChildren();
			return;
		}

		IsEnabled = true;

		bool allSelected = selectableChildren.All(c => c.IsSelected == true);
		bool noneSelected = selectableChildren.All(c => c.IsSelected != true);

		bool? newState;
		if (allSelected && !hasImported)
			newState = true;   // fully checked
		else if (allSelected)
			newState = null;   // all selectable checked but imported items force partial
		else if (noneSelected)
			newState = false;  // nothing selected
		else
			newState = null;   // mixed

		UpdateSelectionSilently(newState);
		Parent?.UpdateSelectionFromChildren();
	}

	public void ApplyFilter(string filter, bool showImported) {
		bool anyVisible = false;
		foreach (var child in Children) {
			if (child is ImportFileViewModel fvm) fvm.ApplyFilter(filter, showImported);
			else if (child is ImportFolderViewModel dvm) dvm.ApplyFilter(filter, showImported);
			if (child.IsVisible) anyVisible = true;
		}

		if (string.IsNullOrWhiteSpace(filter)) {
			// When no search text, hide the folder if all its children are hidden
			IsVisible = anyVisible;
		} else {
			IsVisible = anyVisible || Name.Contains(filter, StringComparison.OrdinalIgnoreCase);
			if (IsVisible) IsExpanded = true;
		}
		BuildNameSegments(filter);
	}

	public override void ApplyFilter(string filter) => ApplyFilter(filter, false);

	// Override to propagate the callback to all children
	private Action? m_selectionCallback;
	public override Action? SelectionChangedCallback {
		get => m_selectionCallback;
		set {
			m_selectionCallback = value;
			foreach (var child in Children)
				child.SelectionChangedCallback = value;
		}
	}

	private IEnumerable<ImportNodeViewModel> SelectableChildren() =>
		Children.Where(c => c.IsEnabled && c is not ImportFileViewModel { IsImported: true } && !c.IsReadOnly);
}

public partial class ImportWindowViewModel : ViewModelBase {
	private readonly List<string> m_allowedExtensions;
	private readonly List<IAssetImporter> m_importers;
	private readonly ImportTreeState m_state;

	[ObservableProperty] private string m_locationPath = "assets://";
	[ObservableProperty] private string m_searchText = string.Empty;
	[ObservableProperty] private bool m_showImported;

	[ObservableProperty] private bool m_isExternal;
	[ObservableProperty] private string m_externalSourcePath = string.Empty;
	[ObservableProperty] private bool m_moveToArtwork = true;
	[ObservableProperty] private string m_artworkDestination = "artwork://";

	private Window? m_window;

	public ImportWindowViewModel() {
		m_state = ImportTreeState.Load();

		m_importers = [
			new TextureImporter(TextureSettings),
			new PsdImporter(TextureSettings, PsdSettings),
			new GltfImporter(GltfSettings, TextureSettings),
		];
		m_allowedExtensions = [];
		foreach (var importer in m_importers)
			m_allowedExtensions.AddRange(importer.SupportedExtensions);

		if (!ProjectContext.IsInitialized) return;

		var artworkDbPath = ProjectContext.Resolve("cache://artwork_database.json");
		var importedFiles = LoadImportedFiles(artworkDbPath);

		var artworkPath = ProjectContext.ArtworkPath;
		if (Directory.Exists(artworkPath)) {
			var rootFolder = new ImportFolderViewModel(artworkPath, null, m_allowedExtensions, importedFiles, m_importers, m_state);
			rootFolder.SelectionChangedCallback = OnSelectionChangedInTree;
			ImportNodes.Add(rootFolder);
		}

		ApplyFilter();
		RebuildSettingsCards();
	}

	private void OnSelectionChangedInTree() => RebuildSettingsCards();

	public ObservableCollection<ImportNodeViewModel> ImportNodes { get; } = [];
	public ObservableCollection<ImporterSettingsCardVM> SettingsCards { get; } = [];

	public TextureImporter.Settings TextureSettings { get; } = new();
	public PsdImporter.Settings PsdSettings { get; } = new();
	public GltfImporter.Settings GltfSettings { get; } = new();

	public void SetWindow(Window window) => m_window = window;

	partial void OnSearchTextChanged(string value) => ApplyFilter();
	partial void OnShowImportedChanged(bool value) => ApplyFilter();

	private void ApplyFilter() {
		foreach (var node in ImportNodes)
			if (node is ImportFolderViewModel folder) folder.ApplyFilter(SearchText, ShowImported);
			else if (node is ImportFileViewModel file) file.ApplyFilter(SearchText, ShowImported);
	}

	[RelayCommand]
	private void ToggleShowImported() => ShowImported = !ShowImported;

	[RelayCommand]
	private void ExpandAll() => SetExpandedAll(ImportNodes, true);

	[RelayCommand]
	private void CollapseAll() {
		var keepExpanded = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		CollectSelectedParentPaths(ImportNodes, keepExpanded);
		SetExpandedAll(ImportNodes, false, keepExpanded);
	}

	private static void CollectSelectedParentPaths(IEnumerable<ImportNodeViewModel> nodes, HashSet<string> paths) {
		foreach (var node in nodes) {
			if (node is ImportFileViewModel { IsSelected: true } file) {
				var parent = file.Parent;
				while (parent != null) {
					paths.Add(parent.FullPath);
					parent = parent.Parent;
				}
			} else if (node is ImportFolderViewModel folder) {
				CollectSelectedParentPaths(folder.Children, paths);
			}
		}
	}

	private static void SetExpandedAll(IEnumerable<ImportNodeViewModel> nodes, bool expanded,
		HashSet<string>? keepExpanded = null) {
		foreach (var node in nodes) {
			if (keepExpanded == null || !keepExpanded.Contains(node.FullPath))
				node.IsExpanded = expanded;
			if (node is ImportFolderViewModel folder)
				SetExpandedAll(folder.Children, expanded, keepExpanded);
		}
	}

	private void RebuildSettingsCards() {
		var extensions = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		CollectSelectedExtensions(ImportNodes, extensions);
		SettingsCards.Clear();
		var seenNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		foreach (var importer in m_importers) {
			if (!importer.SupportedExtensions.Any(ext => extensions.Contains(ext))) continue;
			foreach (var settingsImporter in importer.GetAllSettingsImporters()) {
				if (seenNames.Add(settingsImporter.DisplayName))
					SettingsCards.Add(new ImporterSettingsCardVM(settingsImporter, m_state));
			}
		}
	}

	private static void CollectSelectedExtensions(IEnumerable<ImportNodeViewModel> nodes, HashSet<string> result) {
		foreach (var node in nodes) {
			if (node is ImportFileViewModel { IsSelected: true, IsImported: false } file)
				result.Add(Path.GetExtension(file.FullPath).ToLowerInvariant());
			else if (node is ImportFolderViewModel folder)
				CollectSelectedExtensions(folder.Children, result);
		}
	}

	[RelayCommand]
	private async Task BrowseLocation() {
		if (m_window is null) return;
		var result = await AssetTreePicker.PickFolder(m_window);
		if (result is not null) LocationPath = result;
	}

	[RelayCommand]
	private async Task BrowseExternalSource() {
		if (m_window is null) return;
		var options = new FilePickerOpenOptions {
			Title = "Select artwork file",
			AllowMultiple = false,
			FileTypeFilter = [
				new FilePickerFileType("Artwork files") {
					Patterns = m_allowedExtensions.Select(ext => "*" + ext).ToList()
				},
				FilePickerFileTypes.All
			]
		};
		var files = await m_window.StorageProvider.OpenFilePickerAsync(options);
		if (files.Count > 0) ExternalSourcePath = files[0].TryGetLocalPath() ?? string.Empty;
	}

	[RelayCommand]
	private async Task BrowseArtworkDestination() {
		if (m_window is null) return;
		var result = await AssetTreePicker.PickArtworkFolder(m_window, "Select Artwork Destination");
		if (result is not null) ArtworkDestination = result;
	}

	[RelayCommand]
	private async Task Import() {
		if (IsExternal)
			await ImportExternal();
		else
			await ImportFromTree();
	}

	private async Task ImportFromTree() {
		try {
			var destDir = ProjectContext.Resolve(LocationPath);
			Directory.CreateDirectory(destDir);
		} catch { /* ignore */ }

		var (freshFiles, reimportFiles) = GetSelectedFiles(ImportNodes);
		if (freshFiles.Count == 0 && reimportFiles.Count == 0) return;

		var tasks = new List<LoaderTask>();

		foreach (var file in freshFiles)
			tasks.Add(LoaderTask.DoWithProgress(
				Path.GetFileName(file),
				(log, progress) => ImportSingleFile(file, log, progress)));

		foreach (var file in reimportFiles) {
			var sourceVirtual = ProjectContext.ToVirtual(file);
			if (sourceVirtual is null) continue;
			var captured = sourceVirtual;
			tasks.Add(LoaderTask.DoWithProgress(
				Path.GetFileName(file),
				(log, progress) => AssetDatabase.Reimport(captured, log, progress)));
		}

		await RunImportTasks(tasks);
	}

	private async Task ImportExternal() {
		if (string.IsNullOrWhiteSpace(ExternalSourcePath) || !File.Exists(ExternalSourcePath)) {
			await App.Modals.ShowError("No file selected", "Please select a source file to import.");
			return;
		}

		string realSourcePath = ExternalSourcePath;

		if (MoveToArtwork) {
			// Copy into artwork:// first, then import via the normal path
			var artworkDir = ProjectContext.Resolve(ArtworkDestination);
			Directory.CreateDirectory(artworkDir);
			var destFileName = Path.GetFileName(ExternalSourcePath);
			var artworkDest = Path.Combine(artworkDir, destFileName);
			File.Copy(ExternalSourcePath, artworkDest, overwrite: true);
			realSourcePath = artworkDest;
		}

		var task = LoaderTask.DoWithProgress(Path.GetFileName(realSourcePath), async (log, progress) => {
			if (MoveToArtwork) {
				await ImportSingleFile(realSourcePath, log, progress);
			} else {
				// Import in-place without artwork-DB tracking
				log("Note: source is outside the project — reimport won't be available.");
				await ImportSingleFileAbsolute(realSourcePath, log, progress);
			}
		});
		await RunImportTasks([task]);
	}

	private async Task RunImportTasks(IEnumerable<LoaderTask> tasks) {
		var vm = new LoaderViewModel(tasks) {
			OnComplete = async () => {
				AssetDatabase.RebuildAssetDatabase();
				ProjectContext.RaiseAssetsChanged();
				await Task.CompletedTask;
			}
		};
		var loader = new SimpleLoaderWindow(vm);
		await loader.ShowDialog(m_window!);
		m_window!.Close();
	}

	[RelayCommand]
	private void Cancel() => m_window?.Close();


	private async Task ImportSingleFile(string realSourcePath, Action<string> log, Action<double>? progress = null) {
		try {
			var sourceVirtual = ProjectContext.ToVirtual(realSourcePath)
			                    ?? throw new InvalidOperationException(
				                    $"File is outside artwork path: {realSourcePath}");
			log("Checking if up to date...");
			var hash = AssetDatabase.ComputeHash(realSourcePath);
			if (AssetDatabase.IsUpToDate(sourceVirtual, hash)) {
				log("Already up to date, skipping.");
				return;
			}

			var destDir = ProjectContext.Resolve(LocationPath);
			var ext = Path.GetExtension(realSourcePath).ToLowerInvariant();
			var importer = m_importers.First(i => i.SupportedExtensions.Contains(ext));
			var ctx = new ImportContext { DestDir = destDir, SourceVirtualPath = sourceVirtual };
			var uids = await importer.Import(realSourcePath, ctx, log, progress);
			AssetDatabase.UpdateArtworkDatabase(sourceVirtual, hash, uids);
			log("Done.");
		} catch (Exception ex) {
			log($"ERROR: {ex.Message}");
			await App.Modals.ShowError("Import Failed",
				$"Could not import {Path.GetFileName(realSourcePath)}:\n{ex.Message}");
		}
	}

	private async Task ImportSingleFileAbsolute(string realSourcePath, Action<string> log, Action<double>? progress = null) {
		try {
			var ext = Path.GetExtension(realSourcePath).ToLowerInvariant();
			var importer = m_importers.First(i => i.SupportedExtensions.Contains(ext));
			var destDir = ProjectContext.Resolve(LocationPath);
			// Use a synthetic virtual path so importers don't crash (won't be tracked in DB).
			var fakeVirtual = "artwork://" + Path.GetFileName(realSourcePath);
			var ctx = new ImportContext { DestDir = destDir, SourceVirtualPath = fakeVirtual };
			await importer.Import(realSourcePath, ctx, log, progress);
			log("Done (untracked).");
		} catch (Exception ex) {
			log($"ERROR: {ex.Message}");
			await App.Modals.ShowError("Import Failed",
				$"Could not import {Path.GetFileName(realSourcePath)}:\n{ex.Message}");
		}
	}

	private static HashSet<string> LoadImportedFiles(string dbPath) {
		var result = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		if (!File.Exists(dbPath)) return result;
		try {
			using var doc = JsonDocument.Parse(File.ReadAllText(dbPath));
			if (doc.RootElement.TryGetProperty("type", out var t) && t.GetString() == "artwork_database")
				foreach (var prop in doc.RootElement.EnumerateObject())
					if (prop.Name is not ("type" or "version"))
						result.Add(Path.GetFullPath(ProjectContext.Resolve(prop.Name)));
		} catch (Exception e) {
			Engine.Log.Error(e.Message);
		}
		return result;
	}

	private static (List<string> fresh, List<string> reimport) GetSelectedFiles(IEnumerable<ImportNodeViewModel> nodes) {
		var fresh = new List<string>();
		var reimport = new List<string>();
		foreach (var node in nodes) {
			if (node is ImportFileViewModel { IsSelected: true } file) {
				if (file.IsImported) reimport.Add(file.FullPath);
				else fresh.Add(file.FullPath);
			} else if (node is ImportFolderViewModel folder) {
				var (f, r) = GetSelectedFiles(folder.Children);
				fresh.AddRange(f);
				reimport.AddRange(r);
			}
		}
		return (fresh, reimport);
	}
}
