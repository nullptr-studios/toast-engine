using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.Assets;

namespace editor.Git;

public partial class ArtworkRow : GitFileRow {
	[ObservableProperty] private bool m_isChecked;

	public ArtworkRow(string path, GitService git) : base(path, git.RepoRoot, git.GetAssetStatus(path)) { }
	public bool IsExpanded { get; set; }
}

public partial class ArtworkFolder : ObservableObject {
	private readonly Action<ArtworkFolder> m_expandedChanged;
	[ObservableProperty] private bool m_isExpanded;

	public ArtworkFolder(string name, string key, Action<ArtworkFolder> expandedChanged) {
		Name = name;
		Key = key;
		m_expandedChanged = expandedChanged;
	}

	public string Name { get; }
	public string Key { get; }
	public ObservableCollection<object> Children { get; } = [];
	public List<ArtworkRow> Files { get; } = [];
	public string Count => $"({Files.Count})";

	public bool? IsChecked {
		get {
			var ticked = Files.Count(f => f.IsChecked);
			return ticked == 0 ? false : ticked == Files.Count ? true : null;
		}
		set {
			var target = IsChecked is null ? true : value ?? false;
			foreach (var file in Files) file.IsChecked = target;
		}
	}

	public void RaiseCheckedChanged() {
		OnPropertyChanged(nameof(IsChecked));
	}

	partial void OnIsExpandedChanged(bool value) {
		m_expandedChanged(this);
	}
}

public partial class LockArtworkViewModel : ObservableObject {
	private readonly GitService m_git;
	private readonly HashSet<string> m_expanded = new(StringComparer.OrdinalIgnoreCase);
	private List<ArtworkRow> m_all = [];
	private Dictionary<ArtworkRow, List<ArtworkFolder>> m_ancestors = new();
	private bool m_building;
	private List<ArtworkFolder> m_folders = [];

	[ObservableProperty] private string m_error = "";
	[ObservableProperty] private bool m_isBusy;
	[ObservableProperty] private bool m_isLoading = true;
	[ObservableProperty] private double m_progress;
	[ObservableProperty] private string m_searchText = "";
	[ObservableProperty] private object? m_selectedNode;

	public LockArtworkViewModel(GitService git) {
		m_git = git;
		_ = LoadAsync();
	}

	public ObservableCollection<object> Roots { get; } = [];

	public int CheckedCount => m_all.Count(r => r.IsChecked);
	public bool IsEmpty => !IsLoading && m_all.Count == 0;
	public string LockLabel => CheckedCount == 0 ? "Lock" : $"Lock {CheckedCount} selected";

	public event Action? Done;

	partial void OnSearchTextChanged(string value) {
		BuildTree();
	}

	partial void OnIsBusyChanged(bool value) {
		LockSelectedCommand.NotifyCanExecuteChanged();
	}

	private async Task LoadAsync() {
		IsLoading = true;
		var files = await Task.Run(() => m_git.FindLockableFiles(ProjectContext.ArtworkPath));

		foreach (var row in m_all) row.PropertyChanged -= OnRowChanged;
		m_all = files.OrderBy(f => f, StringComparer.OrdinalIgnoreCase).Select(f => new ArtworkRow(f, m_git)).ToList();
		foreach (var row in m_all) row.PropertyChanged += OnRowChanged;

		IsLoading = false;
		BuildTree();
		NotifySelectionChanged();
		OnPropertyChanged(nameof(IsEmpty));
	}

	private void OnRowChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e) {
		if (e.PropertyName != nameof(ArtworkRow.IsChecked) || sender is not ArtworkRow row) return;

		// Every folder above the file shows ticked, unticked or partly
		if (m_ancestors.TryGetValue(row, out var folders))
			foreach (var folder in folders) folder.RaiseCheckedChanged();
		NotifySelectionChanged();
	}

	private void NotifySelectionChanged() {
		OnPropertyChanged(nameof(CheckedCount));
		OnPropertyChanged(nameof(LockLabel));
		LockSelectedCommand.NotifyCanExecuteChanged();
	}

	private void OnFolderExpanded(ArtworkFolder folder) {
		// A search opens everything so matches are visible
		if (m_building || SearchText.Length > 0) return;
		if (folder.IsExpanded) m_expanded.Add(folder.Key);
		else m_expanded.Remove(folder.Key);
	}

	private void BuildTree() {
		m_building = true;
		var searching = SearchText.Length > 0;
		var terms = SearchText.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
		var root = new ArtworkFolder("", "", OnFolderExpanded);
		var folders = new Dictionary<string, ArtworkFolder>(StringComparer.OrdinalIgnoreCase) { [""] = root };
		var ancestors = new Dictionary<ArtworkRow, List<ArtworkFolder>>();

		foreach (var row in m_all) {
			if (!terms.All(t => row.RelPath.Contains(t, StringComparison.OrdinalIgnoreCase))) continue;

			var relative = Path.GetRelativePath(ProjectContext.ArtworkPath, row.Key);
			var parts = relative.Split(Path.DirectorySeparatorChar, StringSplitOptions.RemoveEmptyEntries);
			var chain = new List<ArtworkFolder> { root };
			var parent = root;
			var key = "";
			for (var i = 0; i < parts.Length - 1; i++) {
				key = key.Length == 0 ? parts[i] : key + "/" + parts[i];
				if (!folders.TryGetValue(key, out var folder)) {
					folder = new ArtworkFolder(parts[i], key, OnFolderExpanded) {
						IsExpanded = searching || m_expanded.Contains(key)
					};
					folders[key] = folder;
					parent.Children.Add(folder);
				}

				chain.Add(folder);
				parent = folder;
			}

			parent.Children.Add(row);
			foreach (var folder in chain) folder.Files.Add(row);
			ancestors[row] = chain;
		}

		m_ancestors = ancestors;
		m_folders = folders.Values.Where(f => !ReferenceEquals(f, root)).ToList();
		foreach (var folder in folders.Values) SortChildren(folder);

		Roots.Clear();
		foreach (var child in root.Children) Roots.Add(child);
		m_building = false;
	}

	// Folders first, then files
	private static void SortChildren(ArtworkFolder folder) {
		var sorted = folder.Children
			.OrderBy(c => c is ArtworkFolder ? 0 : 1)
			.ThenBy(c => c is ArtworkFolder f ? f.Name : ((ArtworkRow)c).Name, StringComparer.OrdinalIgnoreCase)
			.ToList();
		folder.Children.Clear();
		foreach (var child in sorted) folder.Children.Add(child);
	}

	[RelayCommand]
	private void ExpandAll() {
		foreach (var folder in m_folders) folder.IsExpanded = true;
	}

	[RelayCommand]
	private void CollapseAll() {
		foreach (var folder in m_folders) folder.IsExpanded = IsOnPathTo(folder, SelectedNode);
	}

	private bool IsOnPathTo(ArtworkFolder folder, object? selected) {
		return selected switch {
			ArtworkRow row => m_ancestors.TryGetValue(row, out var chain) && chain.Contains(folder),
			ArtworkFolder other => ReferenceEquals(folder, other) || other.Key.StartsWith(folder.Key + "/", StringComparison.OrdinalIgnoreCase),
			_ => false
		};
	}

	private bool CanLock() {
		return !IsBusy && CheckedCount > 0;
	}

	[RelayCommand(CanExecute = nameof(CanLock))]
	private async Task LockSelected() {
		var chosen = m_all.Where(r => r.IsChecked).Select(r => r.Key).ToList();
		if (chosen.Count == 0) return;

		IsBusy = true;
		Error = "";
		Progress = 0;
		try {
			var outcome = await m_git.LockManyAsync(chosen,
				(done, total) => Dispatcher.UIThread.Post(() => Progress = (double)done / total * 100));

			if (outcome.Failed == 0) {
				Done?.Invoke();
				return;
			}

			Error = $"Locked {outcome.Locked} of {chosen.Count}. {outcome.Failed} couldn't be locked, most likely " +
			        $"because someone else locked them first:\n{outcome.FirstError}";
			await LoadAsync(); // what got locked drops off the list, what failed stays
		} finally {
			IsBusy = false;
		}
	}
}
