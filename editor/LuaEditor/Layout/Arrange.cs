using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Graph;

namespace editor.LuaEditor.Layout;

public enum AlignEdge { Top, Left, Bottom, Right }

public static class Arrange {
	public static void Tidy(GraphLayout layout, RegionInfo region, IReadOnlyCollection<GraphNode>? selection, bool includePinned) {
		var ideal = new AutoLayout(layout.Measure, layout.Keys).Region(region);
		var nodes = (selection ?? layout.NodesOf(region)).Where(n => layout.Keys.RegionOf(n) == region && ideal.ContainsKey(n) && !InlineRules.IsMerged(n)).ToList();
		var moving = nodes.Where(n => includePinned || !layout.PinnedNodes.Contains(n)).ToList();
		if (moving.Count == 0) return;

		var shift = default(Vec);
		if (selection is not null) {
			var idealCorner = new Vec(moving.Min(n => ideal[n].X), moving.Min(n => ideal[n].Y));
			var currentCorner = new Vec(moving.Where(layout.IsPlaced).Select(n => layout.Local[n].X).DefaultIfEmpty(idealCorner.X).Min(),
				moving.Where(layout.IsPlaced).Select(n => layout.Local[n].Y).DefaultIfEmpty(idealCorner.Y).Min());
			shift = currentCorner - idealCorner;
		}

		var fixedNodes = layout.NodesOf(region).Where(n => layout.IsPlaced(n) && !moving.Contains(n) && !InlineRules.IsMerged(n)).ToList();
		var placed = new List<Rect>(fixedNodes.Select(layout.BoundsLocal));
		foreach (var node in moving.OrderBy(n => ideal[n].X).ThenBy(n => ideal[n].Y)) {
			var size = layout.Measure.Measure(node);
			var p = ideal[node] + shift;
			var guard = 0;
			while (guard++ < 10_000) {
				var box = new Rect(p.X, p.Y, size.W, size.H).Inflate(4);
				var hit = placed.Where(r => r.Intersects(box)).Select(r => (Rect?)r).FirstOrDefault();
				if (hit is null) break;
				p = new Vec(p.X, hit.Value.Bottom + 8);
			}
			layout.Local[node] = p;
			placed.Add(new Rect(p.X, p.Y, size.W, size.H));
		}
		LayoutEngine.FollowHosts(layout);
	}

	public static bool Straighten(GraphLayout layout, OutputPin from, InputPin to) {
		var source = InlineRules.Drawn(from.Owner);
		var target = InlineRules.Drawn(to.Owner);
		if (NodeShape.SlotOf(from) is not { } fromSlot || NodeShape.SlotOf(to) is not { } toSlot) return false;
		if (!layout.IsPlaced(source) || !layout.IsPlaced(target)) return false;
		var sourceY = layout.Position(source).Y + layout.Measure.PinY(source, fromSlot.Row, true);
		var targetY = layout.Position(target).Y + layout.Measure.PinY(target, toSlot.Row, false);
		var dy = sourceY - targetY;
		if (Math.Abs(dy) < 0.01) return true;

		var moved = layout.Bounds(target).Offset(new Vec(0, dy));
		var region = layout.Keys.RegionOf(target);
		if (region is not null && layout.NodesOf(region).Any(n => n != target && layout.IsPlaced(n) && !InlineRules.IsMerged(n) && layout.Bounds(n).Intersects(moved))) return false;
		layout.SetPosition(target, layout.Position(target) + new Vec(0, dy));
		return true;
	}

	public static bool StraightenSelection(GraphLayout layout, IReadOnlyList<(Slot From, Slot To)> links) {
		var groups = new Dictionary<GraphNode, HashSet<GraphNode>>();
		var obstacles = layout.Local.Keys.Where(node => !layout.IsHidden(node) && !InlineRules.IsMerged(node)).ToList();
		bool changed = false;
		HashSet<GraphNode> Group(GraphNode node) {
			if (!groups.TryGetValue(node, out var group)) groups[node] = group = [node];
			return group;
		}
		double PinY(Slot slot) => layout.Position(slot.Node).Y + layout.Measure.PinY(slot.Node, slot.Row, !slot.IsLeft);
		foreach (var (from, to) in links) {
			var source = from.Node;
			var target = to.Node;
			if (source == target || !layout.IsPlaced(source) || !layout.IsPlaced(target) ||
				layout.IsHidden(source) || layout.IsHidden(target) || layout.HostBody(source) != layout.HostBody(target)) continue;
			var start = Group(source);
			var end = Group(target);
			var dy = PinY(from) - PinY(to);
			if (Math.Abs(dy) > 0.01) {
				if (start == end) continue;
				var delta = new Vec(0, dy);
				if (end.Any(node => layout.ConstrainFrameMove(node, delta) != delta)) continue;
				if (end.Any(node => obstacles.Any(other => !end.Contains(other) &&
					!layout.IsInsideFrameOf(node, other) && !layout.IsInsideFrameOf(other, node) &&
					layout.Bounds(node).Offset(delta).Intersects(layout.Bounds(other))))) continue;
				foreach (var node in end) layout.AnchorFrame(node);
				foreach (var node in end) layout.Local[node] += delta;
				changed = true;
			}
			if (start != end) {
				start.UnionWith(end);
				foreach (var node in end) groups[node] = start;
			}
			if (to.In is { } input) changed |= layout.Reroutes.Remove(input);
			else if (to.Node is Statement statement) changed |= layout.ExecReroutes.Remove(statement);
		}
		LayoutEngine.FollowHosts(layout);
		return changed;
	}

	public static void Align(GraphLayout layout, IReadOnlyCollection<GraphNode> nodes, AlignEdge edge) {
		var placed = nodes.Where(layout.IsPlaced).ToList();
		if (placed.Count < 2) return;
		var bounds = placed.ToDictionary(n => n, layout.Bounds);
		foreach (var node in placed) {
			var b = bounds[node];
			var position = layout.Position(node);
			switch (edge) {
				case AlignEdge.Top: position = position with { Y = bounds.Values.Min(r => r.Y) }; break;
				case AlignEdge.Left: position = position with { X = bounds.Values.Min(r => r.X) }; break;
				case AlignEdge.Bottom: position = position with { Y = bounds.Values.Max(r => r.Bottom) - b.H }; break;
				case AlignEdge.Right: position = position with { X = bounds.Values.Max(r => r.Right) - b.W }; break;
			}
			layout.SetPosition(node, position);
		}
	}

	public static void Distribute(GraphLayout layout, IReadOnlyCollection<GraphNode> nodes, bool horizontal) {
		var placed = nodes.Where(layout.IsPlaced).OrderBy(n => horizontal ? layout.Position(n).X : layout.Position(n).Y).ToList();
		if (placed.Count < 3) return;
		double Extent(GraphNode n) => horizontal ? layout.Bounds(n).W : layout.Bounds(n).H;
		var first = horizontal ? layout.Bounds(placed[0]).X : layout.Bounds(placed[0]).Y;
		var last = horizontal ? layout.Bounds(placed[^1]).Right : layout.Bounds(placed[^1]).Bottom;
		var gap = (last - first - placed.Sum(Extent)) / (placed.Count - 1);
		var cursor = first;
		foreach (var node in placed) {
			var p = layout.Position(node);
			layout.SetPosition(node, horizontal ? p with { X = cursor } : p with { Y = cursor });
			cursor += Extent(node) + gap;
		}
	}
}
