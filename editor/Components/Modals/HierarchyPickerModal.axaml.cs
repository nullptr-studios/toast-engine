using System.Collections.Generic;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Interactivity;
using editor.Components.Elements;
using editor.Engine;
using editor.Workspace;

namespace editor.Components.Modals;

public class HierarchyDisplayItem : SearchableTreeItem<HierarchyDisplayItem> {
	public HierarchyDisplayItem(HierarchyElement element, HierarchyElement? exclude, string? allowedType,
		bool excludedByAncestor = false) {
		Name = element.Name;
		Uid = element.Uid;
		IsExcluded = excludedByAncestor || element == exclude;
		IsTypeAllowed = string.IsNullOrEmpty(allowedType) ||
		                ReflectionDatabase.IsTypeOrSubtypeOf(element.Type, allowedType);
		AllChildren = element.Children.Select(c => new HierarchyDisplayItem(c, exclude, allowedType, IsExcluded)).ToList();
		foreach (var c in AllChildren) FilteredChildren.Add(c);
		InitSegments();
	}

	public string Uid { get; }

	// True for the reparent target and everything below it
	public bool IsExcluded { get; }

	// False when a type filter is active and this node's type does not match
	public bool IsTypeAllowed { get; }

	public bool IsSelectable => !IsExcluded && IsTypeAllowed;
	public double Opacity => IsSelectable ? 1.0 : 0.4;
}

public partial class HierarchyPickerModal : Window {
	private readonly List<HierarchyDisplayItem> m_roots = [];
	private HierarchyDisplayItem? m_selected;

	public HierarchyPickerModal() {
		InitializeComponent();
	}

	public HierarchyPickerModal(IEnumerable<HierarchyElement> roots, HierarchyElement? exclude = null,
		string? allowedType = null) : this() {
		m_roots = roots.Select(r => new HierarchyDisplayItem(r, exclude, allowedType)).ToList();
		NodeTree.ItemsSource = m_roots;
	}

	private void OnSearchChanged(object? sender, TextChangedEventArgs e) {
		var query = SearchBox.Text ?? "";
		foreach (var r in m_roots) r.UpdateFilter(query, query.Any(char.IsUpper));
	}

	private void OnSelectionChanged(object? sender, SelectionChangedEventArgs e) {
		var item = NodeTree.SelectedItem as HierarchyDisplayItem;
		if (item is { IsSelectable: false }) {
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
