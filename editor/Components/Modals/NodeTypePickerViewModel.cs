using System;
using System.Collections;
using System.Linq;
using System.Text.Json;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using editor.Components.Elements;
using editor.Engine;
using Lucide.Avalonia;

namespace editor.Components.Modals;

public class NodeDisplayItem : SearchableTreeItem<NodeDisplayItem> {
	public NodeDisplayItem(NodeTreeItem item) {
		Name = item.Name;
		IsGame = HasAttr(item.Info.Attributes, "Game");
		IsHidden = HasAttr(item.Info.Attributes, "Hidden");
		Color = ReflectionDatabase.ResolveColor(item.Info.Name);
		var iconName = ReflectionDatabase.ResolveIcon(item.Info.Name);
		try {
			Icon = new Bitmap(AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/2x/{iconName}.png")));
		} catch (Exception ex) {
			Log.Warn($"Failed to load icon for node {item.Info.Name}: {ex.Message}");
			Icon = new Bitmap(AssetLoader.Open(new Uri("avares://editor/Resources/node_icons/2x/Circle.png")));
		}

		AllChildren = item.Children.Select(c => new NodeDisplayItem(c)).ToList();
		foreach (var c in AllChildren) FilteredChildren.Add(c);
		InitSegments();
	}

	public bool IsGame { get; }
	public bool IsHidden { get; }
	public string Color { get; }
	public Bitmap? Icon { get; }
	public FontStyle FontStyle => IsGame ? FontStyle.Italic : FontStyle.Normal;
	public double Opacity => IsHidden ? 0.4 : 1.0;
	public IBrush TextColor { get; } = Brushes.White;
	public bool IsExpanded { get; set; } = true;

	private static bool HasAttr(JsonElement attrs, string attrName) {
		return attrs.ValueKind switch {
			JsonValueKind.Array => attrs.EnumerateArray()
				.Any(e => e.ValueKind == JsonValueKind.String && e.GetString() == attrName),
			JsonValueKind.Object => attrs.TryGetProperty(attrName, out _),
			_ => false
		};
	}
}

public class NodeTypePickerViewModel : PickerViewModel {
	private readonly NodeDisplayItem[] m_items;
	private readonly NodeDisplayItem? m_root;

	public NodeTypePickerViewModel() {
		if (ReflectionDatabase.NodeTree is { } tree) {
			m_root = new NodeDisplayItem(tree);
			m_items = [m_root];
		} else {
			m_items = [];
		}
	}

	public override string WindowTitle => "Select Node type...";
	public override IEnumerable Items => m_items;
	public override string AcceptLabel => "Create";
	public override LucideIconKind AcceptIconKind => LucideIconKind.Plus;

	public override void UpdateFilter(string query, bool caseSensitive) {
		m_root?.UpdateFilter(query, caseSensitive);
	}

	public override bool IsSelectable(object? item) {
		return item is NodeDisplayItem { IsHidden: false };
	}

	public override string? GetResult(object? selected) {
		if (selected is not NodeDisplayItem item) return null;
		return ReflectionDatabase.Nodes![item.Name].Namespace + "::" + item.Name;
	}
}
