using System.Collections.Generic;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Interactivity;
using editor.Components.Elements;
using editor.Workspace;

namespace editor.Components.Modals;

public class HierarchyDisplayItem : SearchableTreeItem<HierarchyDisplayItem> {
	public HierarchyDisplayItem(HierarchyElement element, HierarchyElement? exclude, bool excludedByAncestor = false) {
		Name = element.Name;
		Uid = element.Uid;
		IsExcluded = excludedByAncestor || element == exclude;
		AllChildren = element.Children.Select(c => new HierarchyDisplayItem(c, exclude, IsExcluded)).ToList();
		foreach (var c in AllChildren) FilteredChildren.Add(c);
		InitSegments();
	}

	public string Uid { get; }

	// True for the reparent target and everything below it
	public bool IsExcluded { get; }
	public double Opacity => IsExcluded ? 0.4 : 1.0;
}

public partial class HierarchyPickerModal : Window {
	private readonly List<HierarchyDisplayItem> m_roots = [];
	private HierarchyDisplayItem? m_selected;

	public HierarchyPickerModal() {
		InitializeComponent();
	}

	public HierarchyPickerModal(IEnumerable<HierarchyElement> roots, HierarchyElement? exclude = null) : this() {
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

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}
}
