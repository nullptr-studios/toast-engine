using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Interactivity;
using editor.AssetBrowser;

namespace editor.Workspace;

public class HierarchyDisplayItem {
	public string Name { get; }
	public string Uid { get; }
	// True for the reparent target and everything below it
	public bool IsExcluded { get; }
	public List<HierarchyDisplayItem> AllChildren { get; }
	public ObservableCollection<HierarchyDisplayItem> FilteredChildren { get; } = [];
	public ObservableCollection<TextSegment> Segments { get; } = [];

	public double Opacity => IsExcluded ? 0.4 : 1.0;

	public HierarchyDisplayItem(HierarchyElement element, HierarchyElement? exclude, bool excludedByAncestor = false) {
		Name = element.Name;
		Uid = element.Uid;
		IsExcluded = excludedByAncestor || element == exclude;
		// Once excluded, the whole subtree is excluded too
		AllChildren = element.Children.Select(c => new HierarchyDisplayItem(c, exclude, IsExcluded)).ToList();
		foreach (var c in AllChildren) FilteredChildren.Add(c);
		Segments.Add(new TextSegment(Name, false));
	}

	public bool UpdateFilter(string query, bool caseSensitive) {
		if (string.IsNullOrEmpty(query)) {
			FilteredChildren.Clear();
			foreach (var c in AllChildren) FilteredChildren.Add(c);
			Segments.Clear();
			Segments.Add(new TextSegment(Name, false));
			return true;
		}

		var cmp = caseSensitive ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;
		int idx = Name.IndexOf(query, cmp);

		RebuildSegments(idx, query.Length);

		var visible = AllChildren.Where(c => c.UpdateFilter(query, caseSensitive)).ToList();
		FilteredChildren.Clear();
		foreach (var c in visible) FilteredChildren.Add(c);

		return idx >= 0 || visible.Count > 0;
	}

	void RebuildSegments(int idx, int len) {
		Segments.Clear();
		if (idx < 0) {
			Segments.Add(new TextSegment(Name, false));
			return;
		}
		if (idx > 0) Segments.Add(new TextSegment(Name[..idx], false));
		Segments.Add(new TextSegment(Name.Substring(idx, len), true));
		if (idx + len < Name.Length) Segments.Add(new TextSegment(Name[(idx + len)..], false));
	}
}

public partial class HierarchySelect : Window {
	private readonly List<HierarchyDisplayItem> m_roots = [];
	private HierarchyDisplayItem? m_selected;

	public HierarchySelect() {
		InitializeComponent();
	}

	public HierarchySelect(IEnumerable<HierarchyElement> roots, HierarchyElement? exclude = null) : this() {
		m_roots = roots.Select(r => new HierarchyDisplayItem(r, exclude)).ToList();
		NodeTree.ItemsSource = m_roots;
	}

	private void OnSearchChanged(object? sender, TextChangedEventArgs e) {
		var query = SearchBox.Text ?? "";
		foreach (var r in m_roots) r.UpdateFilter(query, query.Any(char.IsUpper));
	}

	private void OnSelectionChanged(object? sender, SelectionChangedEventArgs e) {
		var item = NodeTree.SelectedItem as HierarchyDisplayItem;
		if (item?.IsExcluded == true) {
			NodeTree.SelectedItem = null;
			m_selected = null;
		} else {
			m_selected = item;
		}
	}

	private void OnSelect(object? sender, RoutedEventArgs e) {
		if (m_selected is not null) Close(m_selected.Uid);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) => Close(null);
}
