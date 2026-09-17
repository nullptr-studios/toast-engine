//
// LayoutSerializer.cs by Xein
// 1 Aug 2026
//

using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using editor.Engine;

namespace editor.Workspace;

public static class LayoutSerializer {
	public static LayoutNode? Capture(IDock? dock, IDocumentDock? primary) {
		if (dock is null) return null;

		var node = new LayoutNode {
			Id = dock.Id,
			Proportion = double.IsNaN(dock.Proportion) ? null : dock.Proportion
		};

		switch (dock) {
			case IRootDock root:
				node.Kind = LayoutNode.KindRoot;
				node.ActiveDockable = root.ActiveDockable?.Id;
				node.Children = CaptureChildren(root, primary);
				node.Windows = CaptureWindows(root);
				node.LeftPinned = CaptureIds(root.LeftPinnedDockables);
				node.RightPinned = CaptureIds(root.RightPinnedDockables);
				node.TopPinned = CaptureIds(root.TopPinnedDockables);
				node.BottomPinned = CaptureIds(root.BottomPinnedDockables);
				break;

			case IProportionalDock proportional:
				node.Kind = LayoutNode.KindProportional;
				node.Orientation = proportional.Orientation.ToString();
				node.Children = CaptureChildren(proportional, primary);
				break;

			case IToolDock tool:
				node.Kind = LayoutNode.KindTool;
				node.Alignment = tool.Alignment.ToString();
				node.GripMode = tool.GripMode.ToString();
				node.Dockables = CaptureIds(tool.VisibleDockables) ?? [];
				node.ActiveDockable = tool.ActiveDockable?.Id;
				break;

			case IDocumentDock document:
				node.Kind = LayoutNode.KindDocument;
				node.IsPrimary = ReferenceEquals(document, primary);
				break;

			default:
				return null;
		}

		return node;
	}

	private static List<LayoutNode> CaptureChildren(IDock dock, IDocumentDock? primary) {
		var children = new List<LayoutNode>();
		if (dock.VisibleDockables is null) return children;

		foreach (var dockable in dock.VisibleDockables)
			switch (dockable) {
				case IProportionalDockSplitter:
					children.Add(new LayoutNode { Kind = LayoutNode.KindSplitter });
					break;
				case IDock child when Capture(child, primary) is { } node:
					children.Add(node);
					break;
			}

		return children;
	}

	private static List<LayoutWindowNode> CaptureWindows(IRootDock root) {
		var windows = new List<LayoutWindowNode>();
		if (root.Windows is null) return windows;

		foreach (var window in root.Windows) {
			if (Capture(window.Layout, null) is not { } layout) continue;
			windows.Add(new LayoutWindowNode {
				Id = window.Id,
				X = Finite(window.X),
				Y = Finite(window.Y),
				Width = Finite(window.Width),
				Height = Finite(window.Height),
				Topmost = window.Topmost,
				Title = window.Title,
				Layout = layout
			});
		}

		return windows;
	}

	private static List<string>? CaptureIds(IList<IDockable>? dockables) {
		if (dockables is null) return null;
		return dockables
			.Select(d => d.Id)
			.Where(id => !string.IsNullOrEmpty(id))
			.ToList();
	}

	public static IRootDock? BuildRoot(
		LayoutNode? node,
		Factory factory,
		Func<string, IDockable?> resolve,
		IDocumentDock? primary = null,
		Action<IToolDock>? configureToolDock = null) {
		if (node is null || node.Kind != LayoutNode.KindRoot) return null;
		return Build(node, new BuildContext(factory, resolve, primary, configureToolDock)) as IRootDock;
	}

	private static IDockable? Build(LayoutNode node, BuildContext ctx) {
		return node.Kind switch {
			LayoutNode.KindRoot => BuildRootDock(node, ctx),
			LayoutNode.KindProportional => BuildProportional(node, ctx),
			LayoutNode.KindSplitter => ctx.Factory.CreateProportionalDockSplitter(),
			LayoutNode.KindTool => BuildToolDock(node, ctx),
			LayoutNode.KindDocument => BuildDocumentDock(node, ctx),
			_ => null
		};
	}

	private static IDockable BuildRootDock(LayoutNode node, BuildContext ctx) {
		var root = ctx.Factory.CreateRootDock();
		root.Id = node.Id ?? "Root";
		root.IsCollapsable = false;
		root.Proportion = node.Proportion ?? double.NaN;

		var children = BuildChildren(node, ctx);
		root.VisibleDockables = ctx.Factory.CreateList(children.ToArray());
		root.HiddenDockables = ctx.Factory.CreateList<IDockable>();
		root.Windows = ctx.Factory.CreateList<IDockWindow>();
		root.DefaultDockable = children.FirstOrDefault(c => c is IDock);
		root.ActiveDockable = ResolveActive(children, node.ActiveDockable);
		root.FocusedDockable = null;

		BuildPinned(root, node, children, ctx);
		BuildWindows(root, node, ctx);
		return root;
	}

	private static IDockable BuildProportional(LayoutNode node, BuildContext ctx) {
		var dock = ctx.Factory.CreateProportionalDock();
		dock.Id = node.Id ?? "";
		dock.Orientation = ParseEnum(node.Orientation, Orientation.Horizontal);
		dock.Proportion = node.Proportion ?? double.NaN;

		var children = BuildChildren(node, ctx);
		dock.VisibleDockables = ctx.Factory.CreateList(children.ToArray());
		dock.ActiveDockable = ResolveActive(children, node.ActiveDockable);
		dock.FocusedDockable = null;
		return dock;
	}

	private static IDockable BuildToolDock(LayoutNode node, BuildContext ctx) {
		var dock = ctx.Factory.CreateToolDock();
		dock.Id = node.Id ?? "";
		dock.Alignment = ParseEnum(node.Alignment, Alignment.Unset);
		dock.GripMode = ParseEnum(node.GripMode, GripMode.Visible);
		dock.Proportion = node.Proportion ?? double.NaN;

		var tools = ResolveAll(node.Dockables, ctx).ToList();
		dock.VisibleDockables = ctx.Factory.CreateList(tools.ToArray());
		dock.ActiveDockable = ResolveActive(tools, node.ActiveDockable);
		dock.FocusedDockable = null;

		ctx.ConfigureToolDock?.Invoke(dock);
		return dock;
	}

	private static IDockable? BuildDocumentDock(LayoutNode node, BuildContext ctx) {
		if (!node.IsPrimary || ctx.Primary is null) return null;
		ctx.Primary.Proportion = node.Proportion ?? double.NaN;
		return ctx.Primary;
	}

	private static List<IDockable> BuildChildren(LayoutNode node, BuildContext ctx) {
		var children = new List<IDockable>();
		if (node.Children is null) return children;
		foreach (var child in node.Children)
			if (Build(child, ctx) is { } built)
				children.Add(built);

		return TrimSplitters(children);
	}

	private static List<IDockable> TrimSplitters(List<IDockable> children) {
		var trimmed = new List<IDockable>(children.Count);
		foreach (var child in children) {
			if (child is not IProportionalDockSplitter) {
				trimmed.Add(child);
				continue;
			}

			if (trimmed.Count == 0 || trimmed[^1] is IProportionalDockSplitter) continue;
			trimmed.Add(child);
		}

		if (trimmed.Count > 0 && trimmed[^1] is IProportionalDockSplitter) trimmed.RemoveAt(trimmed.Count - 1);
		return trimmed;
	}

	private static void BuildPinned(IRootDock root, LayoutNode node, List<IDockable> children, BuildContext ctx) {
		root.LeftPinnedDockables = PinnedList(node.LeftPinned, children, ctx);
		root.RightPinnedDockables = PinnedList(node.RightPinned, children, ctx);
		root.TopPinnedDockables = PinnedList(node.TopPinned, children, ctx);
		root.BottomPinnedDockables = PinnedList(node.BottomPinned, children, ctx);
	}

	private static IList<IDockable>? PinnedList(List<string>? ids, List<IDockable> children, BuildContext ctx) {
		if (ids is null || ids.Count == 0) return null;
		// a tool cannot be pinned and docked at the same time
		var pinned = ResolveAll(ids, ctx)
			.Where(d => !IsInTree(children, d))
			.ToArray();
		return pinned.Length == 0 ? null : ctx.Factory.CreateList(pinned);
	}

	private static void BuildWindows(IRootDock root, LayoutNode node, BuildContext ctx) {
		if (node.Windows is null || node.Windows.Count == 0) return;

		foreach (var windowNode in node.Windows) {
			if (windowNode.Layout is null) continue;
			if (Build(windowNode.Layout, ctx) is not IRootDock layout) continue;
			if (!HasAnyDockable(layout)) continue;

			var (x, y) = ClampToScreens(windowNode.X, windowNode.Y, windowNode.Width, windowNode.Height);

			var window = ctx.Factory.CreateDockWindow();
			window.Id = windowNode.Id ?? "Window";
			window.Title = windowNode.Title ?? "";
			window.X = x;
			window.Y = y;
			window.Width = windowNode.Width;
			window.Height = windowNode.Height;
			window.Topmost = windowNode.Topmost;
			window.Owner = root;
			window.Layout = layout;
			layout.Window = window;

			root.Windows?.Add(window);
		}
	}

	private static IEnumerable<IDockable> ResolveAll(List<string>? ids, BuildContext ctx) {
		if (ids is null) yield break;
		var seen = new HashSet<IDockable>();
		foreach (var id in ids) {
			if (string.IsNullOrEmpty(id)) continue;
			if (ctx.Resolve(id) is not { } dockable) {
				Log.Warn($"Layout: unknown dockable '{id}', dropped");
				continue;
			}

			if (seen.Add(dockable)) yield return dockable;
		}
	}

	private static IDockable? ResolveActive(List<IDockable> children, string? id) {
		var candidates = children.Where(c => c is not IProportionalDockSplitter).ToList();
		if (candidates.Count == 0) return null;
		if (string.IsNullOrEmpty(id)) return candidates[0];
		return candidates.FirstOrDefault(c => c.Id == id) ?? candidates[0];
	}

	private static bool IsInTree(List<IDockable> children, IDockable target) {
		foreach (var child in children) {
			if (ReferenceEquals(child, target)) return true;
			if (child is IDock dock && ContainsVisible(dock, target)) return true;
		}

		return false;
	}

	public static bool ContainsVisible(IDock? dock, IDockable target) {
		if (dock?.VisibleDockables is null) return false;
		foreach (var dockable in dock.VisibleDockables) {
			if (ReferenceEquals(dockable, target)) return true;
			if (dockable is IDock child && ContainsVisible(child, target)) return true;
		}

		return false;
	}

	private static bool HasAnyDockable(IDock dock) {
		if (dock.VisibleDockables is null) return false;
		foreach (var dockable in dock.VisibleDockables) {
			if (dockable is IProportionalDockSplitter) continue;
			if (dockable is not IDock child) return true;
			if (HasAnyDockable(child)) return true;
		}

		return false;
	}

	public static IEnumerable<IDock> EnumerateDocks(IDock? dock, bool includeWindows = true) {
		if (dock is null) yield break;
		yield return dock;

		if (dock.VisibleDockables is not null)
			foreach (var dockable in dock.VisibleDockables)
				if (dockable is IDock child)
					foreach (var nested in EnumerateDocks(child, includeWindows))
						yield return nested;

		if (includeWindows && dock is IRootDock { Windows: not null } root)
			foreach (var window in root.Windows)
			foreach (var nested in EnumerateDocks(window.Layout, includeWindows))
				yield return nested;
	}

	private static T ParseEnum<T>(string? value, T fallback) where T : struct, Enum {
		return Enum.TryParse<T>(value, out var parsed) ? parsed : fallback;
	}

	private static double Finite(double value) {
		return double.IsFinite(value) ? value : 0;
	}

	private static (double X, double Y) ClampToScreens(double x, double y, double width, double height) {
		var screens = App.MainWindow?.Screens;
		if (screens is null || screens.All.Count == 0) return (x, y);

		var origin = new PixelPoint((int)x, (int)y);
		foreach (var screen in screens.All)
			if (screen.Bounds.Contains(origin))
				return (x, y);

		var area = (screens.Primary ?? screens.All[0]).WorkingArea;
		return (
			Math.Clamp(x, area.X, Math.Max(area.X, area.X + area.Width - Math.Max(width, 100))),
			Math.Clamp(y, area.Y, Math.Max(area.Y, area.Y + area.Height - Math.Max(height, 100)))
		);
	}

	private sealed record BuildContext(
		Factory Factory,
		Func<string, IDockable?> Resolve,
		IDocumentDock? Primary,
		Action<IToolDock>? ConfigureToolDock);
}
