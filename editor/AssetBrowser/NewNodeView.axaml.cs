using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using editor.Services;

namespace editor.AssetBrowser;

public class TextSegment(string text, bool isHighlight) {
	public string Text { get; } = text;
	public FontWeight FontWeight => isHighlight ? FontWeight.Bold : FontWeight.Regular;
	public IBrush? Highlight => isHighlight ? new SolidColorBrush(Color.FromRgb(0x33, 0x77, 0xCC)) : null;
}

public class NodeDisplayItem {
	public string Name { get; }
	public bool IsGame { get; }
	public bool IsHidden { get; }
	public List<NodeDisplayItem> AllChildren { get; }
	public ObservableCollection<NodeDisplayItem> FilteredChildren { get; } = [];
	public ObservableCollection<TextSegment> Segments { get; } = [];

	public bool HasIcon { get; }
	public FontStyle FontStyle => IsGame ? FontStyle.Italic : FontStyle.Normal;
	public double Opacity => IsHidden ? 0.4 : 1.0;
	public IBrush TextColor { get; } = Brushes.White; // TODO: parse Color attribute and tint icon + text

	public NodeDisplayItem(NodeTreeItem item) {
		Name = item.Name;
		IsGame = HasAttr(item.Info.Attributes, "Game");
		IsHidden = HasAttr(item.Info.Attributes, "Hidden");
		HasIcon = HasAttr(item.Info.Attributes, "Icon");
		// TODO: if HasAttr(item.Info.Attributes, "Color"), parse color string and set TextColor
		AllChildren = item.Children.Select(c => new NodeDisplayItem(c)).ToList();
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

	static bool HasAttr(JsonElement attrs, string attrName) =>
		attrs.ValueKind switch {
			JsonValueKind.Array  => attrs.EnumerateArray().Any(e => e.ValueKind == JsonValueKind.String && e.GetString() == attrName),
			JsonValueKind.Object => attrs.TryGetProperty(attrName, out _),
			_                   => false
		};
}

public partial class NewNodeView : Window {
	private NodeDisplayItem? m_root;
	private NodeDisplayItem? m_selected;

	public NewNodeView() {
		InitializeComponent();
		if (ReflectionDatabase.NodeTree is null) return;
		m_root = new NodeDisplayItem(ReflectionDatabase.NodeTree);
		NodeTree.ItemsSource = new[] { m_root };
	}

	private void OnSearchChanged(object? sender, TextChangedEventArgs e) {
		var query = SearchBox.Text ?? "";
		m_root?.UpdateFilter(query, query.Any(char.IsUpper));
	}

	private void OnSelectionChanged(object? sender, SelectionChangedEventArgs e) {
		var item = NodeTree.SelectedItem as NodeDisplayItem;
		if (item?.IsHidden == true) {
			NodeTree.SelectedItem = null;
			m_selected = null;
		} else {
			m_selected = item;
		}
	}

	private void OnCreate(object? sender, RoutedEventArgs e) {
		if (m_selected is not null) Close(ReflectionDatabase.Nodes![m_selected.Name].Namespace + "::" + m_selected.Name);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) => Close(null);
}