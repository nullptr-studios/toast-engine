using System;
using System.Collections;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets;
using Lucide.Avalonia;

namespace editor.Components.Modals;

public partial class AssetFolderNode : ObservableObject {
	[ObservableProperty] private bool m_isExpanded = true;

	public AssetFolderNode(string realPath) {
		RealPath = realPath;
		Name = Path.GetFileName(realPath) is { Length: > 0 } n ? n : realPath;
		foreach (var dir in Directory.EnumerateDirectories(realPath)) {
			var child = new AssetFolderNode(dir);
			Children.Add(child);
			FilteredChildren.Add(child);
		}
	}

	public string Name { get; }
	public string RealPath { get; }
	public ObservableCollection<AssetFolderNode> Children { get; } = [];
	public ObservableCollection<AssetFolderNode> FilteredChildren { get; } = [];

	public bool UpdateFilter(string query, bool caseSensitive) {
		if (string.IsNullOrEmpty(query)) {
			FilteredChildren.Clear();
			foreach (var c in Children) {
				c.UpdateFilter(query, caseSensitive);
				FilteredChildren.Add(c);
			}

			return true;
		}

		var cmp = caseSensitive ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;
		var nameMatch = Name.Contains(query, cmp);
		var visible = Children.Where(c => c.UpdateFilter(query, caseSensitive)).ToList();
		FilteredChildren.Clear();
		foreach (var c in visible) FilteredChildren.Add(c);
		return nameMatch || visible.Count > 0;
	}
}

public class AssetFolderTreeViewModel : PickerViewModel {
	private readonly ObservableCollection<AssetFolderNode> m_filtered = [];
	private readonly ObservableCollection<AssetFolderNode> m_roots = [];

	public AssetFolderTreeViewModel(bool useArtwork = false) {
		if (ProjectContext.IsInitialized) {
			var rootPath = useArtwork ? ProjectContext.ArtworkPath : ProjectContext.AssetsPath;
			var root = new AssetFolderNode(rootPath);
			m_roots.Add(root);
			m_filtered.Add(root);
		}
	}

	public AssetFolderNode? SelectedFolder { get; set; }

	public override string WindowTitle => "Select Destination Folder";
	public override IEnumerable Items => m_filtered;
	public override string? ExtraButtonLabel => "New Folder";
	public override LucideIconKind ExtraIconKind => LucideIconKind.FolderPlus;

	public override void UpdateFilter(string query, bool caseSensitive) {
		var visible = m_roots.Where(r => r.UpdateFilter(query, caseSensitive)).ToList();
		m_filtered.Clear();
		foreach (var r in visible) m_filtered.Add(r);
	}

	public override bool IsSelectable(object? item) {
		return item is AssetFolderNode;
	}

	public override string? GetResult(object? selected) {
		if (selected is not AssetFolderNode node) return null;
		SelectedFolder = node;
		return ProjectContext.ToVirtual(node.RealPath);
	}

	public override async Task OnExtraButton(Window owner, object? selected) {
		var folderName = await new NewFolderModal().ShowDialog<string?>(owner);
		if (string.IsNullOrWhiteSpace(folderName)) return;

		var parent = selected as AssetFolderNode ?? m_roots.FirstOrDefault();
		if (parent is null) return;

		var newPath = Path.Combine(parent.RealPath, folderName);
		Directory.CreateDirectory(newPath);
		var newNode = new AssetFolderNode(newPath);
		parent.Children.Insert(0, newNode);
		parent.FilteredChildren.Insert(0, newNode);
	}
}
