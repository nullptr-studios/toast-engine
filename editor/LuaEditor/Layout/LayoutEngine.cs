using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Graph;

namespace editor.LuaEditor.Layout;

public static class LayoutEngine {
	public static GraphLayout Create(LuaGraph graph, INodeMeasure? measure = null) {
		var layout = new GraphLayout(graph, measure ?? new DefaultMeasure());
		PlaceUnplaced(layout);
		PackRegions(layout);
		return layout;
	}

	public static void PlaceUnplaced(GraphLayout layout) {
		var auto = new AutoLayout(layout.Measure, layout.Keys);
		foreach (var region in layout.Keys.Regions) {
			var nodes = layout.NodesOf(region);
			if (nodes.All(n => layout.IsPlaced(n) || InlineRules.IsMerged(n))) continue;
			var ideal = auto.Region(region);
			if (!nodes.Any(n => layout.IsPlaced(n) && !InlineRules.IsMerged(n))) {
				foreach (var (node, p) in ideal) layout.Local[node] = p;
				continue;
			}

			var anchor = nodes.First(n => layout.IsPlaced(n) && !InlineRules.IsMerged(n));
			var shift = ideal.TryGetValue(anchor, out var idealAnchor) ? layout.Local[anchor] - idealAnchor : default;
			new Filler(layout, region, ideal, shift).Run();
		}
		FollowHosts(layout);
	}

	public static void FollowHosts(GraphLayout layout) {
		foreach (var (node, _) in layout.Keys.Ordered) {
			if (!InlineRules.IsMerged(node)) continue;
			if (layout.Local.TryGetValue(InlineRules.Drawn(node), out var host)) layout.Local[node] = host;
		}
	}

	public static bool Separate(GraphLayout layout, IEnumerable<GraphNode> placed, bool includeInputs = true) {
		var group = new HashSet<GraphNode>();
		foreach (var node in placed.Select(InlineRules.Drawn)) {
			if (!layout.IsPlaced(node)) continue;
			group.Add(node);
			if (includeInputs && layout.Keys.KeyOf(node) is { } key && node is Statement)
				foreach (var (other, otherKey) in layout.Keys.Ordered)
					if (otherKey.StartsWith(key + "#", StringComparison.Ordinal) && !InlineRules.IsMerged(other) && layout.IsPlaced(other)) group.Add(other);
		}
		if (group.Count == 0) return false;
		// a frame must not collide with its own contents
		var others = layout.Local.Keys.Where(n => !group.Contains(n) && !InlineRules.IsMerged(n) && !layout.IsHidden(n) &&
			(layout.Loose.Contains(n) || layout.Keys.KeyOf(n) is not null) &&
			!group.Any(g => layout.IsInsideFrameOf(g, n) || layout.IsInsideFrameOf(n, g) ||
				NodeShape.ExpansionOf(n) is not null && Contains(layout.Bounds(n), layout.Bounds(g)))).ToList();
		var waiting = layout.Loose.OfType<Statement>().Where(n => n.IsFloating && !group.Contains(n) &&
			group.Any(g => layout.IsInsideFrameOf(n, g))).ToList();
		var moved = false;
		for (var guard = 0; guard < 400; guard++) {
			var rects = group.Select(layout.Bounds).Select(r => r.Inflate(4)).ToList();
			var hit = others.Select(layout.Bounds).Where(b => rects.Any(r => r.Intersects(b))).Select(b => (Rect?)b).FirstOrDefault();
			if (hit is null) break;
			var dy = hit.Value.Bottom + 8 - rects.Min(r => r.Y) - 4;
			foreach (var node in group) layout.Local[node] += new Vec(0, dy);
			foreach (var node in waiting) layout.Local[node] += new Vec(0, dy);
			moved = true;
		}
		FollowHosts(layout);
		return moved;
	}

	private static bool Contains(Rect outer, Rect inner) => inner.X >= outer.X && inner.X <= outer.Right && inner.Y >= outer.Y + DefaultMeasure.HeaderHeight && inner.Y <= outer.Bottom;

	public static void PlaceNewRegions(GraphLayout layout) {
		var fresh = layout.Keys.Regions.Where(r => !layout.Embedded.ContainsKey(r.Body) && !layout.Origins.ContainsKey(r.Body) && layout.RegionExtent(r) is not null).ToList();
		if (fresh.Count == 0) return;
		var others = layout.Keys.Regions.Where(r => !fresh.Contains(r) && !layout.Embedded.ContainsKey(r.Body) && layout.RegionExtent(r) is not null).Select(r => layout.RegionExtent(r)!.Value).ToList();
		var x = others.Count == 0 ? 0 : others.Min(r => r.X);
		var y = others.Count == 0 ? 0 : others.Max(r => r.Bottom) + 80;
		foreach (var region in fresh) {
			layout.PlaceRegion(region, new Vec(x, y));
			y = layout.RegionExtent(region)!.Value.Bottom + 80;
		}
	}

	public static void PackRegions(GraphLayout layout) {
		var regions = layout.Keys.Regions.Where(r => !layout.Embedded.ContainsKey(r.Body) && layout.RegionExtent(r) is not null).ToList();
		var free = regions.Where(r => !layout.PinnedRegions.Contains(r.Body)).ToList();
		var module = free.Where(r => r.IsModule);
		var rest = free.Where(r => !r.IsModule).Select((r, i) => (r, i))
			.OrderBy(t => RegionPacker.LifecycleRank(t.r.Header is FunctionDecl d ? d.Name : "")).ThenBy(t => t.i).Select(t => t.r);
		var ordered = module.Concat(rest).ToList();

		var obstacles = regions.Where(r => layout.PinnedRegions.Contains(r.Body)).Select(r => layout.RegionExtent(r)!.Value).ToList();
		var sizes = ordered.Select(r => {
			var b = layout.RegionExtent(r)!.Value;
			return layout.CollapsedRegions.Contains(r.Body) ? new Size(Math.Min(b.W, 260), DefaultMeasure.HeaderHeight + 8) : new Size(b.W, b.H);
		}).ToList();
		var positions = RegionPacker.Pack(sizes, obstacles);
		for (var i = 0; i < ordered.Count; i++) layout.PlaceRegion(ordered[i], positions[i]);
	}

	private sealed class Filler {
		private readonly GraphLayout m_layout;
		private readonly RegionInfo m_region;
		private readonly Dictionary<GraphNode, Vec> m_ideal;
		private readonly Vec m_shift;
		private readonly GraphKeys m_keys;

		public Filler(GraphLayout layout, RegionInfo region, Dictionary<GraphNode, Vec> ideal, Vec shift) {
			m_layout = layout;
			m_region = region;
			m_ideal = ideal;
			m_shift = shift;
			m_keys = layout.Keys;
		}

		private bool IsPlaced(GraphNode node) => m_layout.Local.ContainsKey(node);

		private Vec At(GraphNode node) => m_layout.Local[node];

		private Size SizeOf(GraphNode node) => m_layout.Measure.Measure(node);

		private Rect Box(GraphNode node) {
			var p = At(node);
			var s = SizeOf(node);
			return new Rect(p.X, p.Y, s.W, s.H);
		}

		public void Run() {
			foreach (var node in new[] { m_region.Header, m_region.Entry }) {
				if (node is null || IsPlaced(node) || InlineRules.IsMerged(node) || m_keys.RegionOf(node) != m_region) continue;
				PlaceGroup(node, [], m_ideal.GetValueOrDefault(node) + m_shift);
			}
			Block(m_region.Body, StartOf(m_region.Body));
		}

		private Vec StartOf(Block block) {
			var first = block.Statements.FirstOrDefault(IsPlaced);
			if (first is not null) return At(first);
			if (block.Owner is FunctionEntry entry && InlineRules.Drawn(entry) is var header && IsPlaced(header)) return At(header) + new Vec(SizeOf(header).W + AutoLayout.StepGap, 0);
			if (block.Owner is { } owner && IsPlaced(owner)) return At(owner) + new Vec(SizeOf(owner).W + AutoLayout.StepGap, 0);
			var min = m_ideal.Count == 0 ? default : new Vec(m_ideal.Values.Min(v => v.X), m_ideal.Values.Min(v => v.Y));
			return min + m_shift;
		}

		private void Block(Block block, Vec start) {
			var cursor = start;
			foreach (var statement in block.Statements) {
				if (statement is FunctionDecl) continue;
				if (IsPlaced(statement)) {
					FillData(statement);
				} else {
					var data = DataOf(statement);
					var dataWidth = data.Count == 0 ? 0 : m_ideal[statement].X - data.Min(n => m_ideal[n].X);
					var target = new Vec(cursor.X + dataWidth, cursor.Y);
					var below = PlacedBelow(statement);
					if (below.Count > 0) target = new Vec(below.Min(n => At(n).X) - SizeOf(statement).W - AutoLayout.StepGap, below.Min(n => At(n).Y));
					PlaceGroup(statement, data, target);
				}

				var position = At(statement);
				var childY = position.Y;
				var childX = position.X + SizeOf(statement).W + AutoLayout.StepGap;
				foreach (var child in statement.ChildBlocks) {
					Block(child, new Vec(childX, childY));
					var bottom = BottomOf(child);
					childY = Math.Max(childY, double.IsNegativeInfinity(bottom) ? childY : bottom) + AutoLayout.RowGap;
				}
				cursor = new Vec(RightOf(statement) + AutoLayout.StepGap, position.Y);
			}
		}

		private List<Statement> PlacedBelow(Statement statement) {
			var list = new List<Statement>();
			foreach (var child in statement.ChildBlocks)
				foreach (var inner in child.Statements) {
					if (IsPlaced(inner)) list.Add(inner);
					else list.AddRange(PlacedBelow(inner));
				}
			return list;
		}

		private List<GraphNode> DataOf(Statement statement) {
			var list = new List<GraphNode>();
			var seen = new HashSet<GraphNode>();
			void Visit(InputPin pin) {
				if (pin.Source?.Owner is not ExprNode e || !seen.Add(e) || m_keys.RegionOf(e) != m_region) return;
				if (!InlineRules.IsMerged(e) && !IsPlaced(e)) list.Add(e);
				if (e is FunctionNode) return;
				foreach (var input in e.Inputs) Visit(input);
			}
			foreach (var pin in statement.Inputs) Visit(pin);
			return list;
		}

		private void FillData(Statement statement) {
			var missing = DataOf(statement);
			if (missing.Count == 0) return;
			var offset = At(statement) - (m_ideal.GetValueOrDefault(statement) + m_shift);
			foreach (var node in missing) PlaceGroup(node, [], m_ideal.GetValueOrDefault(node) + m_shift + offset);
		}

		private void PlaceGroup(GraphNode main, List<GraphNode> others, Vec target) {
			var mainIdeal = m_ideal.GetValueOrDefault(main);
			var positions = new List<(GraphNode Node, Vec Pos)> { (main, target) };
			foreach (var node in others) positions.Add((node, target + (m_ideal[node] - mainIdeal)));
			var members = positions.Select(p => p.Node).ToHashSet();

			for (var guard = 0; guard < 10_000; guard++) {
				var group = positions.Select(p => new Rect(p.Pos.X, p.Pos.Y, SizeOf(p.Node).W, SizeOf(p.Node).H).Inflate(6)).ToList();
				var hit = Collider(group, members);
				if (hit is null) break;
				var dy = hit.Value.Bottom - group.Min(g => g.Y) + 4;
				positions = positions.Select(p => (p.Node, p.Pos + new Vec(0, dy))).ToList();
			}
			foreach (var (node, pos) in positions) m_layout.Local[node] = pos;
		}

		private Rect? Collider(List<Rect> group, HashSet<GraphNode> except) {
			foreach (var node in m_layout.NodesOf(m_region)) {
				if (except.Contains(node) || !IsPlaced(node) || InlineRules.IsMerged(node)) continue;
				var box = Box(node);
				if (group.Any(g => g.Intersects(box))) return box;
			}
			return null;
		}

		private double RightOf(Statement statement) {
			var right = Box(statement).Right;
			foreach (var child in statement.ChildBlocks)
				foreach (var inner in child.Statements.Where(IsPlaced))
					right = Math.Max(right, RightOf(inner));
			return right;
		}

		private double BottomOf(Block block) {
			var bottom = double.NegativeInfinity;
			foreach (var statement in block.Statements.Where(IsPlaced)) {
				bottom = Math.Max(bottom, Box(statement).Bottom);
				foreach (var child in statement.ChildBlocks) bottom = Math.Max(bottom, BottomOf(child));
			}
			return bottom;
		}
	}
}
