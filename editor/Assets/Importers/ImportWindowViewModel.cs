using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Avalonia.Controls;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.Components.Modals;

namespace editor.Assets.Importers;

public abstract partial class ImportNodeViewModel : ViewModelBase {
	[ObservableProperty] private string m_fullPath = string.Empty;
	[ObservableProperty] private bool m_isEnabled = true;
	[ObservableProperty] private bool m_isExpanded = true;
	[ObservableProperty] private bool m_isReadOnly;

	private bool? m_isSelected = false;
	[ObservableProperty] private bool m_isVisible = true;
	[ObservableProperty] private string m_name = string.Empty;

	protected ImportNodeViewModel(string path, ImportFolderViewModel? parent) {
		FullPath = Path.GetFullPath(path);
		Name = Path.GetFileName(path);
		Parent = parent;
	}

	// tri-state: true = selected, false = deselected, null = mixed (folder with partial children)
	public bool? IsSelected {
		get => m_isSelected;
		set {
			if (SetProperty(ref m_isSelected, value)) OnSelectionChanged(value);
		}
	}

	public ImportFolderViewModel? Parent { get; }

	protected virtual void OnSelectionChanged(bool? value) { }

	public void ToggleSelection() {
		if (!IsEnabled || IsReadOnly) return;
		IsSelected = IsSelected != true;
	}

	// sets the backing field without firing OnSelectionChanged
	// used by folders when bubbling state up to avoid infinite recursion
	public void UpdateSelectionSilently(bool? value) {
		SetProperty(ref m_isSelected, value, nameof(IsSelected));
	}

	public abstract void ApplyFilter(string filter);
}

public class ImportFileViewModel : ImportNodeViewModel {
	public ImportFileViewModel(
		string path, ImportFolderViewModel? parent,
		List<string> allowedExtensions, HashSet<string> importedFiles)
		: base(path, parent) {
		var ext = Path.GetExtension(path).ToLowerInvariant();
		if (importedFiles.Contains(path) || importedFiles.Contains(FullPath)) {
			// already imported -> show as checked but locked so the user knows its there
			IsReadOnly = true;
			IsSelected = true;
		} else if (!allowedExtensions.Contains(ext)) {
			// unsupported extension -> greyed out, cant be selected
			IsEnabled = false;
			IsSelected = false;
		}
	}

	protected override void OnSelectionChanged(bool? value) {
		Parent?.UpdateSelectionFromChildren();
	}

	public override void ApplyFilter(string filter) {
		IsVisible = string.IsNullOrWhiteSpace(filter) ||
		            Name.Contains(filter, StringComparison.OrdinalIgnoreCase);
	}
}

public class ImportFolderViewModel : ImportNodeViewModel {
	public ImportFolderViewModel(
		string path, ImportFolderViewModel? parent,
		List<string> allowedExtensions, HashSet<string> importedFiles)
		: base(path, parent) {
		foreach (var dir in Directory.EnumerateDirectories(path))
			Children.Add(new ImportFolderViewModel(dir, this, allowedExtensions, importedFiles));
		foreach (var file in Directory.EnumerateFiles(path))
			Children.Add(new ImportFileViewModel(file, this, allowedExtensions, importedFiles));
		IsEnabled = Children.Any(c => c.IsEnabled);
		UpdateSelectionFromChildren();
	}

	public ObservableCollection<ImportNodeViewModel> Children { get; } = [];

	protected override void OnSelectionChanged(bool? value) {
		if (value == null) return;
		// propagate down (skip readonly and disabled children)
		foreach (var child in Children.Where(c => !c.IsReadOnly && c.IsEnabled))
			child.IsSelected = value;
		Parent?.UpdateSelectionFromChildren();
	}

	// recalculates this folder's tri-state from its children and bubbles up
	// all checked -> true, all unchecked -> false, mixed -> null
	public void UpdateSelectionFromChildren() {
		bool allChecked = true, allUnchecked = true, anyEnabled = false;
		foreach (var child in Children) {
			if (!child.IsEnabled) continue;
			anyEnabled = true;
			if (child.IsSelected == true) {
				allUnchecked = false;
			} else if (child.IsSelected == false) {
				allChecked = false;
			} else {
				allChecked = false;
				allUnchecked = false;
			}
		}

		IsEnabled = anyEnabled;
		bool? newState = !anyEnabled ? false : allChecked ? true : allUnchecked ? false : null;
		UpdateSelectionSilently(newState); // silent so we dont trigger another bubble
		Parent?.UpdateSelectionFromChildren();
	}

	public override void ApplyFilter(string filter) {
		if (string.IsNullOrWhiteSpace(filter)) {
			foreach (var child in Children) child.ApplyFilter(filter);
			IsVisible = true;
			return;
		}

		var anyVisible = false;
		foreach (var child in Children) {
			child.ApplyFilter(filter);
			if (child.IsVisible) anyVisible = true;
		}

		// keep the folder visible if its name matches OR any child is visible
		// also expand it so matching children are visible without manual clicking
		IsVisible = anyVisible || Name.Contains(filter, StringComparison.OrdinalIgnoreCase);
		if (IsVisible) IsExpanded = true;
	}
}

public partial class ImportWindowViewModel : ViewModelBase {
	private readonly List<string> m_allowedExtensions;
	private readonly List<IAssetImporter> m_importers;
	[ObservableProperty] private string m_locationPath = "assets://";
	[ObservableProperty] private string m_searchText = string.Empty;

	private Window? m_window;

	public ImportWindowViewModel() {
		m_importers = [
			new TextureImporter(TextureSettings),
			new PsdImporter(TextureSettings, PsdSettings),
			new GltfImporter(GltfSettings, TextureSettings),
		];
		m_allowedExtensions = [];
		foreach (var importer in m_importers)
			m_allowedExtensions.AddRange(importer.SupportedExtensions);

		if (!ProjectContext.IsInitialized) return;

		var artworkPath = ProjectContext.ArtworkPath;
		var artworkDbPath = ProjectContext.Resolve("cache://artwork_database.json");
		var importedFiles = LoadImportedFiles(artworkDbPath);

		if (Directory.Exists(artworkPath))
			ImportNodes.Add(new ImportFolderViewModel(artworkPath, null, m_allowedExtensions, importedFiles));
	}

	public ObservableCollection<ImportNodeViewModel> ImportNodes { get; } = [];
	public TextureImporter.Settings TextureSettings { get; } = new();
	public PsdImporter.Settings PsdSettings { get; } = new();
	public GltfImporter.Settings GltfSettings { get; } = new();

	public void SetWindow(Window window) {
		m_window = window;
	}

	partial void OnSearchTextChanged(string value) {
		foreach (var node in ImportNodes) node.ApplyFilter(value);
	}

	// reads already-imported source paths from the artwork database
	// so we can mark them as read-only in the tree (user sees they were imported before)
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

	[RelayCommand]
	private async Task BrowseLocation() {
		if (m_window is null) return;
		var result = await AssetTreePicker.PickFolder(m_window!);
		if (result is not null) LocationPath = result;
	}

	[RelayCommand]
	private async Task Import() {
		try {
			var destDir = ProjectContext.Resolve(LocationPath);
			Directory.CreateDirectory(destDir);
		} catch {
			// ignore: folder creation failure won't stop import
		}

		var selectedFiles = GetSelectedFiles(ImportNodes);
		if (selectedFiles.Count == 0) return;

		var tasks = selectedFiles.Select(file => LoaderTask.Do(
			Path.GetFileName(file),
			async log => await ImportSingleFile(file, log)
		));

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
	private void Cancel() {
		m_window?.Close();
	}

	private async Task ImportSingleFile(string realSourcePath, Action<string> log) {
		try {
			var sourceVirtual = ProjectContext.ToVirtual(realSourcePath)
			                    ?? throw new InvalidOperationException($"File is outside artwork path: {realSourcePath}");

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
			var uids = await importer.Import(realSourcePath, ctx, log);

			AssetDatabase.UpdateArtworkDatabase(sourceVirtual, hash, uids);
			log("Done.");
		} catch (Exception ex) {
			log($"ERROR: {ex.Message}");
			await App.Modals.ShowError(
				"Import Failed",
				$"Could not import {Path.GetFileName(realSourcePath)}:\n{ex.Message}");
		}
	}

	// recurses into folders, skips read-only files (already imported)
	private static List<string> GetSelectedFiles(IEnumerable<ImportNodeViewModel> nodes) {
		var list = new List<string>();
		foreach (var node in nodes)
			if (node is ImportFileViewModel { IsSelected: true, IsReadOnly: false } file)
				list.Add(file.FullPath);
			else if (node is ImportFolderViewModel folder)
				list.AddRange(GetSelectedFiles(folder.Children));
		return list;
	}
}
