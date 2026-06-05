using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Tomlyn.Model;

namespace editor.Import;

public abstract partial class ImportNodeViewModel : ViewModelBase {
	[ObservableProperty]
	private string m_name = string.Empty;

	[ObservableProperty]
	private string m_fullPath = string.Empty;

	private bool? m_isSelected = false;
	public bool? IsSelected {
		get => m_isSelected;
		set {
			if (SetProperty(ref m_isSelected, value)) {
				OnSelectionChanged(value);
			}
		}
	}

	[ObservableProperty]
	private bool m_isEnabled = true;

	[ObservableProperty]
	private bool m_isReadOnly = false;

	[ObservableProperty]
	private bool m_isExpanded = true;

	[ObservableProperty]
	private bool m_isVisible = true;

	public ImportFolderViewModel? Parent { get; }

	protected ImportNodeViewModel(string path, ImportFolderViewModel? parent) {
		FullPath = Path.GetFullPath(path);
		Name = Path.GetFileName(path);
		Parent = parent;
	}

	protected virtual void OnSelectionChanged(bool? value) { }

	public void ToggleSelection() {
		if (!IsEnabled || IsReadOnly) return;
		// Users can only toggle between True and False. Partial (null) is automatic.
		IsSelected = IsSelected != true;
	}

	public void UpdateSelectionSilently(bool? value) {
		SetProperty(ref m_isSelected, value, nameof(IsSelected));
	}

	public abstract void ApplyFilter(string filter);
}

public partial class ImportFileViewModel : ImportNodeViewModel {
	public ImportFileViewModel(string path, ImportFolderViewModel? parent, List<string> allowedExtensions, HashSet<string> importedFiles)
		: base(path, parent) {
		var ext = Path.GetExtension(path).ToLowerInvariant();
		
		if (importedFiles.Contains(path) || importedFiles.Contains(FullPath)) {
			IsReadOnly = true;
			IsSelected = true;
			IsEnabled = true;
		} else if (!allowedExtensions.Contains(ext)) {
			IsEnabled = false;
			IsSelected = false;
		}
	}

	protected override void OnSelectionChanged(bool? value) {
		Parent?.UpdateSelectionFromChildren();
	}

	public override void ApplyFilter(string filter) {
		if (string.IsNullOrWhiteSpace(filter)) {
			IsVisible = true;
			return;
		}
		IsVisible = Name.Contains(filter, StringComparison.OrdinalIgnoreCase);
	}
}

public partial class ImportFolderViewModel : ImportNodeViewModel {
	public ObservableCollection<ImportNodeViewModel> Children { get; } = [];

	public ImportFolderViewModel(string path, ImportFolderViewModel? parent, List<string> allowedExtensions, HashSet<string> importedFiles)
		: base(path, parent) {
		
		foreach (var dir in Directory.EnumerateDirectories(path)) {
			Children.Add(new ImportFolderViewModel(dir, this, allowedExtensions, importedFiles));
		}

		foreach (var file in Directory.EnumerateFiles(path)) {
			Children.Add(new ImportFileViewModel(file, this, allowedExtensions, importedFiles));
		}

		IsEnabled = Children.Any(c => c.IsEnabled);
		UpdateSelectionFromChildren();
	}

	protected override void OnSelectionChanged(bool? value) {
		if (value == null) return;

		foreach (var child in Children) {
			if (!child.IsReadOnly && child.IsEnabled) {
				child.IsSelected = value;
			}
		}
		
		Parent?.UpdateSelectionFromChildren();
	}

	public void UpdateSelectionFromChildren() {
		bool allChecked = true;
		bool allUnchecked = true;
		bool anyEnabled = false;

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

		bool? newState;
		if (!anyEnabled) newState = false;
		else if (allChecked) newState = true;
		else if (allUnchecked) newState = false;
		else newState = null;

		UpdateSelectionSilently(newState);
		Parent?.UpdateSelectionFromChildren();
	}

	public override void ApplyFilter(string filter) {
		if (string.IsNullOrWhiteSpace(filter)) {
			foreach (var child in Children) child.ApplyFilter(filter);
			IsVisible = true;
			return;
		}

		bool anyChildVisible = false;
		foreach (var child in Children) {
			child.ApplyFilter(filter);
			if (child.IsVisible) anyChildVisible = true;
		}

		IsVisible = anyChildVisible || Name.Contains(filter, StringComparison.OrdinalIgnoreCase);
		if (IsVisible) IsExpanded = true;
	}
}

public partial class ImportWindowViewModel : ViewModelBase {
	private readonly List<string> m_allowedExtensions = [
		".png", ".tga",
	];

	public ObservableCollection<ImportNodeViewModel> ImportNodes { get; } = [];

	[ObservableProperty]
	private string m_searchText = string.Empty;

	partial void OnSearchTextChanged(string value) {
		foreach (var node in ImportNodes) {
			node.ApplyFilter(value);
		}
	}

	public ImportWindowViewModel() {
		var artworkPath = @"C:\Users\Xein\Desktop\unnamed_project\artwork";
		var artworkDatabasePath = @"C:\Users\Xein\Desktop\unnamed_project\.toast\artwork_database.json";

		HashSet<string> importedFiles = [];
		if (File.Exists(artworkDatabasePath)) {
			try {
				var json = File.ReadAllText(artworkDatabasePath);
				using var doc = JsonDocument.Parse(json);
				if (doc.RootElement.TryGetProperty("type", out var type) && type.GetString() == "artwork_database") {
					foreach (var property in doc.RootElement.EnumerateObject()) {
						if (property.Name is not ("type" or "version")) {
							importedFiles.Add(Path.GetFullPath(property.Name));
						}
					}
				}
			} catch {
				// Handle parse errors silently for now
			}
		}

		if (Directory.Exists(artworkPath)) {
			ImportNodes.Add(new ImportFolderViewModel(artworkPath, null, m_allowedExtensions, importedFiles));
		}
	}

	[RelayCommand]
	private void Import() {
		var selectedFiles = GetSelectedFiles(ImportNodes);

		foreach (var file in selectedFiles) {
			var meta = CreateMeta(file);
			ImportFile();
			UpdateDatabases(file);
		}
		// TODO: Process files
		Console.WriteLine($"Importing {selectedFiles.Count} files:");
		foreach (var file in selectedFiles) {
			Console.WriteLine($"- {file}");
		}
	}

	private TomlTable CreateMeta(string file) {

	}

	[RelayCommand]
	private void Cancel() {
		// This requires a way to close the window, usually via a service or event
	}

	private List<string> GetSelectedFiles(IEnumerable<ImportNodeViewModel> nodes) {
		var list = new List<string>();
		foreach (var node in nodes) {
			if (node is ImportFileViewModel file && file.IsSelected == true && !file.IsReadOnly) {
				list.Add(file.FullPath);
			} else if (node is ImportFolderViewModel folder) {
				list.AddRange(GetSelectedFiles(folder.Children));
			}
		}
		return list;
	}
}
