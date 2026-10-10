using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using editor.Engine;

namespace editor.Components.Modals;

public interface IPickerNode {
	bool IsExpanded { get; set; }
	bool IsFolder => false;
	IEnumerable VisibleChildren { get; }
	string? ThumbnailUid => null;
}

public sealed class PickerRow {
	private const double IndentStep = 16;

	private PickerRow(object node, int depth, bool hasChildren) {
		Node = node;
		HasChildren = hasChildren;
		var picker = node as IPickerNode;
		IsExpanded = picker?.IsExpanded ?? false;
		IsFolder = picker?.IsFolder ?? false;
		Indent = new Thickness(depth * IndentStep, 0, 0, 0);
	}

	public object Node { get; }
	public bool HasChildren { get; }
	public bool IsExpanded { get; }
	public bool IsFolder { get; }
	public Thickness Indent { get; }
	public string? ThumbnailUid => (Node as IPickerNode)?.ThumbnailUid;

	public static List<PickerRow> Flatten(IEnumerable roots) {
		var rows = new List<PickerRow>();
		foreach (var root in roots) Add(rows, root, 0);
		return rows;
	}

	private static void Add(List<PickerRow> rows, object node, int depth) {
		if (node is not IPickerNode picker) {
			rows.Add(new PickerRow(node, depth, false));
			return;
		}

		var children = picker.VisibleChildren.Cast<object>().ToList();
		rows.Add(new PickerRow(node, depth, children.Count > 0));
		if (!picker.IsExpanded) return;
		foreach (var child in children) Add(rows, child, depth + 1);
	}
}

internal static class NodeIconCache {
	private static readonly Dictionary<string, Bitmap> s_icons = [];

	public static Bitmap Get(string iconName) {
		if (s_icons.TryGetValue(iconName, out var cached)) return cached;
		Bitmap icon;
		try {
			icon = Load(iconName);
		} catch (Exception ex) {
			Log.Warn($"Failed to load node icon {iconName}: {ex.Message}");
			icon = s_icons.TryGetValue("Circle", out var circle) ? circle : Load("Circle");
			s_icons["Circle"] = icon;
		}

		s_icons[iconName] = icon;
		return icon;
	}

	private static Bitmap Load(string iconName) {
		return new Bitmap(AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/2x/{iconName}.png")));
	}
}
