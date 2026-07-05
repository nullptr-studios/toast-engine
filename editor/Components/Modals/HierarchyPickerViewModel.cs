using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using editor.Components.Elements;
using editor.Engine;
using editor.Workspace;

namespace editor.Components.Modals;

public class HierarchyDisplayItem : SearchableTreeItem<HierarchyDisplayItem> {
	public HierarchyDisplayItem(
		HierarchyElement element, HierarchyElement? exclude, string? allowedType,
		bool excludedByAncestor = false) {
		Name = element.Name;
		Uid = element.Uid;
		Type = element.Type;
		IsExcluded = excludedByAncestor || element == exclude;
		IsTypeAllowed = string.IsNullOrEmpty(allowedType) ||
			ReflectionDatabase.IsTypeOrSubtypeOf(element.Type, allowedType);

		Color = ReflectionDatabase.ResolveColor(element.Type);
		var iconName = ReflectionDatabase.ResolveIcon(element.Type);
		try {
			Icon = new Bitmap(AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/2x/{iconName}.png")));
		} catch (Exception ex) {
			Log.Warn($"Failed to load icon for node type {element.Type}: {ex.Message}");
			Icon = new Bitmap(AssetLoader.Open(new Uri("avares://editor/Resources/node_icons/2x/Circle.png")));
		}

		AllChildren = element.Children.Select(c => new HierarchyDisplayItem(c, exclude, allowedType, IsExcluded))
			.ToList();
		foreach (var c in AllChildren) FilteredChildren.Add(c);
		InitSegments();
	}

	public string Uid { get; }
	public string Type { get; }
	public string Color { get; }
	public Bitmap? Icon { get; }
	public bool IsExcluded { get; }
	public bool IsTypeAllowed { get; }
	public bool IsSelectable => !IsExcluded && IsTypeAllowed;
	public double Opacity => IsSelectable ? 1.0 : 0.4;
	public bool IsExpanded { get; set; } = true;
}

public class HierarchyPickerViewModel : PickerViewModel {
	private readonly List<HierarchyDisplayItem> m_roots;

	public HierarchyPickerViewModel(
		IEnumerable<HierarchyElement> roots,
		HierarchyElement? exclude = null, string? allowedType = null) {
		m_roots = roots.Select(r => new HierarchyDisplayItem(r, exclude, allowedType)).ToList();
	}

	public override string WindowTitle => "Select a Node...";
	public override IEnumerable Items => m_roots;

	public override void UpdateFilter(string query, bool caseSensitive) {
		foreach (var r in m_roots) r.UpdateFilter(query, caseSensitive);
	}

	public override bool IsSelectable(object? item) {
		return item is HierarchyDisplayItem { IsSelectable: true };
	}

	public override string? GetResult(object? selected) {
		return (selected as HierarchyDisplayItem)?.Uid;
	}
}
