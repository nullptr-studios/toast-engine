using System.Linq;
using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using editor.Components.Elements;
using editor.Engine;

namespace editor.Components.Modals;

public class NodeDisplayItem : SearchableTreeItem<NodeDisplayItem> {
	public NodeDisplayItem(NodeTreeItem item) {
		Name = item.Name;
		IsGame = HasAttr(item.Info.Attributes, "Game");
		IsHidden = HasAttr(item.Info.Attributes, "Hidden");
		HasIcon = HasAttr(item.Info.Attributes, "Icon");
		AllChildren = item.Children.Select(c => new NodeDisplayItem(c)).ToList();
		foreach (var c in AllChildren) FilteredChildren.Add(c);
		InitSegments();
	}

	public bool IsGame { get; }
	public bool IsHidden { get; }
	public bool HasIcon { get; }
	public FontStyle FontStyle => IsGame ? FontStyle.Italic : FontStyle.Normal;
	public double Opacity => IsHidden ? 0.4 : 1.0;
	public IBrush TextColor { get; } = Brushes.White;

	private static bool HasAttr(JsonElement attrs, string attrName) {
		return attrs.ValueKind switch {
			JsonValueKind.Array => attrs.EnumerateArray()
				.Any(e => e.ValueKind == JsonValueKind.String && e.GetString() == attrName),
			JsonValueKind.Object => attrs.TryGetProperty(attrName, out _),
			_ => false
		};
	}
}

public partial class NodeTypePickerModal : Window {
	private readonly NodeDisplayItem? m_root;
	private NodeDisplayItem? m_selected;

	public NodeTypePickerModal() {
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

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}
}
