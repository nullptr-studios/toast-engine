using System;
using System.Collections;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets;
using Lucide.Avalonia;

namespace editor.Components.Modals;

public partial class AssetTreeNode : ObservableObject {
	[ObservableProperty] private bool m_isExpanded = true;

	public AssetTreeNode(string realPath, string? typeFilter) {
		RealPath = realPath;
		Name = Path.GetFileName(realPath) is { Length: > 0 } n ? n : realPath;
		IsFolder = true;
		TypeColor = Brushes.Transparent;
		TypeLabel = "";

		var folder = new AssetFolder(realPath);
		foreach (var sub in folder.SubFolders) {
			var child = new AssetTreeNode(sub.Filepath, typeFilter);
			Children.Add(child);
			FilteredChildren.Add(child);
		}

		foreach (var file in folder.Files) {
			if (typeFilter is not null &&
			    !string.Equals(file.Definition?.Type, typeFilter, StringComparison.OrdinalIgnoreCase)) continue;
			if (file.Uid is null) continue;
			var child = new AssetTreeNode(file);
			Children.Add(child);
			FilteredChildren.Add(child);
		}
	}

	public AssetTreeNode(AssetFile file) {
		RealPath = file.Filepath;
		Name = file.Name;
		IsFolder = false;
		Uid = file.Uid;
		TypeColor = file.TypeColor;
		TypeLabel = file.TypeLabel;
	}

	public bool IsFolder { get; }
	public bool IsAsset => !IsFolder;
	public string Name { get; }
	public string RealPath { get; }
	public string? Uid { get; }
	public IBrush TypeColor { get; }
	public string TypeLabel { get; }
	public ObservableCollection<AssetTreeNode> Children { get; } = [];
	public ObservableCollection<AssetTreeNode> FilteredChildren { get; } = [];

	public bool UpdateFilter(string query, bool caseSensitive) {
		if (string.IsNullOrEmpty(query)) {
			FilteredChildren.Clear();
			foreach (var c in Children) {
				c.UpdateFilter(query, caseSensitive);
				FilteredChildren.Add(c);
			}

			return true;
		}

		if (!IsFolder) {
			var cmp = caseSensitive ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;
			return Name.Contains(query, cmp);
		}

		var visible = Children.Where(c => c.UpdateFilter(query, caseSensitive)).ToList();
		FilteredChildren.Clear();
		foreach (var c in visible) FilteredChildren.Add(c);
		return visible.Count > 0;
	}
}

public class AssetTreeViewModel : PickerViewModel {
	private readonly string? m_filter;
	private readonly ObservableCollection<AssetTreeNode> m_filtered = [];
	private readonly ObservableCollection<AssetTreeNode> m_roots = [];

	public AssetTreeViewModel(string? typeFilter = null) {
		m_filter = typeFilter;
		if (ProjectContext.IsInitialized) {
			var root = new AssetTreeNode(ProjectContext.AssetsPath, typeFilter);
			m_roots.Add(root);
			m_filtered.Add(root);
		}
	}

	public override string WindowTitle => "Select an Asset...";
	public override IEnumerable Items => m_filtered;
	public override string? ExtraButtonLabel => "New Folder";
	public override LucideIconKind ExtraIconKind => LucideIconKind.FolderPlus;

	public override void UpdateFilter(string query, bool caseSensitive) {
		var visible = m_roots.Where(r => r.UpdateFilter(query, caseSensitive)).ToList();
		m_filtered.Clear();
		foreach (var r in visible) m_filtered.Add(r);
	}

	public override bool IsSelectable(object? item) {
		return item is AssetTreeNode { IsAsset: true };
	}

	public override string? GetResult(object? selected) {
		return selected is AssetTreeNode { IsAsset: true } node ? node.Uid : null;
	}

	public override async Task OnExtraButton(Window owner, object? selected) {
		var folderName = await new NewFolderModal().ShowDialog<string?>(owner);
		if (string.IsNullOrWhiteSpace(folderName)) return;

		var parent = (selected as AssetTreeNode)?.IsFolder == true ? (AssetTreeNode)selected! : m_roots.FirstOrDefault();
		if (parent is null) return;

		var newPath = Path.Combine(parent.RealPath, folderName);
		Directory.CreateDirectory(newPath);
		var newNode = new AssetTreeNode(newPath, m_filter);
		parent.Children.Insert(0, newNode);
		parent.FilteredChildren.Insert(0, newNode);
	}
}
