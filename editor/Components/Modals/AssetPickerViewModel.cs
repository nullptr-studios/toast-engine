using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia.Media;
using editor.Assets;
using editor.Components.Elements;

namespace editor.Components.Modals;

public class AssetPickerItem : IPickerNode {
	public AssetPickerItem(
		string uid, string name, string path, IBrush typeColor, string typeLabel, bool hasThumbnail,
		bool showPath = true) {
		Uid = uid;
		Name = name;
		Path = path;
		TypeColor = typeColor;
		TypeLabel = typeLabel;
		HasThumbnail = hasThumbnail;
		ShowPath = showPath;
		Segments = [new TextSegment(name, false)];
	}

	public string Uid { get; }
	public string Name { get; }
	public string Path { get; }
	public IBrush TypeColor { get; }
	public string TypeLabel { get; }
	public bool HasThumbnail { get; }
	public bool ShowPath { get; }
	public IReadOnlyList<TextSegment> Segments { get; private set; }

	public bool IsExpanded { get; set; }
	IEnumerable IPickerNode.VisibleChildren => Array.Empty<object>();
	string? IPickerNode.ThumbnailUid => HasThumbnail ? Uid : null;

	public bool UpdateSegments(string query, bool caseSensitive) {
		if (string.IsNullOrEmpty(query)) {
			Segments = [new TextSegment(Name, false)];
			return true;
		}

		var cmp = caseSensitive ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;
		var idx = Name.IndexOf(query, cmp);
		if (idx < 0) return false;

		var segments = new List<TextSegment>(3);
		if (idx > 0) segments.Add(new TextSegment(Name[..idx], false));
		segments.Add(new TextSegment(Name.Substring(idx, query.Length), true));
		if (idx + query.Length < Name.Length) segments.Add(new TextSegment(Name[(idx + query.Length)..], false));
		Segments = segments;
		return true;
	}
}

public class AssetTreeNode : IPickerNode {
	private readonly List<object> m_children = [];
	private readonly List<object> m_visible = [];

	private AssetTreeNode(string name) {
		Name = name;
	}

	public string Name { get; }
	public bool IsExpanded { get; set; } = true;
	public bool IsFolder => true;
	IEnumerable IPickerNode.VisibleChildren => m_visible;

	public static AssetTreeNode? Build(string name, AssetFolder folder, Func<AssetFile, bool> accept) {
		var node = new AssetTreeNode(name);
		foreach (var sub in PickerOrdering.ByName(folder.SubFolders, sub => sub.Name))
			if (Build(sub.Name, sub, accept) is { } child)
				node.m_children.Add(child);

		foreach (var file in PickerOrdering.ByName(folder.Files, file => file.Name).Where(accept)) {
			var real = file.Filepath[..^5];
			node.m_children.Add(new AssetPickerItem(
				file.Uid!, file.Name, ProjectContext.ToVirtual(real) ?? real,
				file.TypeColor, file.TypeLabel, file.Definition?.HasThumbnail == true, false));
		}

		if (node.m_children.Count == 0) return null;
		node.m_visible.AddRange(node.m_children);
		return node;
	}

	public bool UpdateFilter(string query, bool caseSensitive) {
		m_visible.Clear();
		foreach (var child in m_children) {
			var matched = child switch {
				AssetTreeNode folder => folder.UpdateFilter(query, caseSensitive),
				AssetPickerItem item => item.UpdateSegments(query, caseSensitive),
				_ => false
			};
			if (matched) m_visible.Add(child);
		}

		return m_visible.Count > 0;
	}
}

public class AssetPickerViewModel : PickerViewModel {
	// save last mode
	private static bool s_treeMode;

	private readonly List<AssetPickerItem> m_all = [];
	private readonly List<AssetPickerItem> m_filtered = [];
	private readonly List<AssetTreeNode> m_roots = [];
	private readonly List<AssetTreeNode> m_visibleRoots = [];
	private bool m_caseSensitive;
	private string m_query = "";

	public AssetPickerViewModel(string? assetType, string? extraType = null) {
		bool Accept(AssetFile file) {
			if (assetType is not null
			    && !string.Equals(file.Definition?.Type, assetType, StringComparison.OrdinalIgnoreCase)
			    && !string.Equals(file.Definition?.Type, extraType, StringComparison.OrdinalIgnoreCase)) return false;
			return file.Uid is not null && !file.Filepath[..^5].EndsWith(".d.lua", StringComparison.OrdinalIgnoreCase);
		}

		if (ProjectContext.IsInitialized) {
			foreach (var root in ProjectContext.DatabaseRoots.Append(ProjectContext.CorePath)) {
				if (!Directory.Exists(root)) continue;
				var folder = new AssetFolder(root);
				var name = Path.GetFileName(root) is { Length: > 0 } n ? n : root;
				if (AssetTreeNode.Build(name, folder, Accept) is { } node) m_roots.Add(node);

				foreach (var file in Flatten(folder).Where(Accept)) {
					var real = file.Filepath[..^5];
					m_all.Add(new AssetPickerItem(
						file.Uid!, file.Name, ProjectContext.ToVirtual(real) ?? real,
						file.TypeColor, file.TypeLabel, file.Definition?.HasThumbnail == true));
				}
			}
		}

		m_all.Sort((a, b) => {
			var result = PickerOrdering.CompareNames(a.Name, b.Name);
			return result != 0 ? result : PickerOrdering.CompareNames(a.Path, b.Path);
		});
		m_roots.Sort((a, b) => PickerOrdering.CompareNames(a.Name, b.Name));

		m_filtered.AddRange(m_all);
		m_visibleRoots.AddRange(m_roots);
		IsTreeMode = s_treeMode;
	}

	public override string WindowTitle => "Select an Asset...";
	public override IEnumerable Items => IsTreeMode ? m_visibleRoots : m_filtered;
	public override bool HasViewToggle => true;

	public override bool IsTreeMode {
		get => s_treeMode;
		set {
			if (s_treeMode == value) return;
			s_treeMode = value;
			ApplyFilter();
		}
	}

	private static IEnumerable<AssetFile> Flatten(AssetFolder folder) {
		foreach (var file in folder.Files) yield return file;
		foreach (var sub in folder.SubFolders)
		foreach (var file in Flatten(sub))
			yield return file;
	}

	public override void UpdateFilter(string query, bool caseSensitive) {
		m_query = query;
		m_caseSensitive = caseSensitive;
		ApplyFilter();
	}

	private void ApplyFilter() {
		if (s_treeMode) {
			m_visibleRoots.Clear();
			m_visibleRoots.AddRange(m_roots.Where(r => r.UpdateFilter(m_query, m_caseSensitive)));
		} else {
			m_filtered.Clear();
			m_filtered.AddRange(m_all.Where(i => i.UpdateSegments(m_query, m_caseSensitive)));
		}
	}

	public override bool IsSelectable(object? item) {
		return item is AssetPickerItem;
	}

	public override string? GetResult(object? selected) {
		return (selected as AssetPickerItem)?.Uid;
	}
}
