using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text.RegularExpressions;
using System.Windows.Input;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;

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

	public static AssetBrowserViewModel? Current { get; private set; }

	public AssetBrowserViewModel() {
		Current = this;
		RefreshCommand = new RelayCommand(Refresh);
		ExpandAllCommand = new RelayCommand(() => SetAllExpanded(true));
		CollapseAllCommand = new RelayCommand(() => SetAllExpanded(false));
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

	private static AssetFolder? FindParentFolder(AssetFolder folder, AssetFile file) {
		if (folder.Files.Contains(file)) return folder;
		foreach (var sub in folder.SubFolders)
			if (FindParentFolder(sub, file) is { } found)
				return found;
		return null;
	}

	// the database can be rebuilt from background work, so dispatch the reload back onto the UI thread
	private void OnDatabaseReloaded() {
		Dispatcher.UIThread.Post(LoadFolders);
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
			m_searchText = value;
			if (!string.IsNullOrWhiteSpace(value)) {
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

	public void UpdateSelection(IList? items) {
		m_selectedCount = items?.Count ?? 0;
		Notify(nameof(ItemCount));
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
		m_selectedFolder = null;
		Folders.Clear();
		Notify(nameof(SelectedFolder));
		Notify(nameof(BreadcrumbItems));

		// fallback paths for the designer (never reached at runtime)
		var assetsPath = ProjectContext.IsInitialized
			? ProjectContext.AssetsPath
			: @"C:\Users\Xein\Desktop\unnamed_project\assets";
		var corePath = ProjectContext.IsInitialized
			? ProjectContext.CorePath
			: @"C:\Users\Xein\code\toast-engine\out\Debug\toast_engine\bin\assets";

		var assetsFolder = new AssetFolder(assetsPath);
		assetsFolder.Name = "assets://";
		assetsFolder.IsExpanded = true;
		Folders.Add(assetsFolder);

		var coreFolder = new AssetFolder(corePath);
		coreFolder.Name = "core://";
		Folders.Add(coreFolder);

		Notify(nameof(CurrentItems));
		Notify(nameof(ItemCount));
	}

	private void Refresh() {
		LoadFolders();
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
