using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Graph;

namespace editor.LuaEditor.Layout;

public sealed class LayoutGroup {
	public string Title { get; set; } = "";
	public string Color { get; set; } = "#4a7a5c";

	public Rect Rect { get; set; }

	public List<GraphNode> Members { get; } = [];
}

public sealed class GraphLayout {
	private Dictionary<RegionInfo, List<GraphNode>> m_nodesOfRegion = [];

	public GraphLayout(LuaGraph graph, INodeMeasure measure) {
		Graph = graph;
		Measure = measure;
		Keys = GraphKeys.Compute(graph);
		IndexRegions();
		if (measure is DefaultMeasure sized) sized.RegionSize = CalleeSize;
	}

	private readonly HashSet<FunctionDecl> m_measuring = [];

	private Size? CalleeSize(FunctionDecl callee) {
		var region = Keys.Regions.FirstOrDefault(r => r.Header == callee);
		if (region is null || !m_measuring.Add(callee)) return null;
		try {
			if (LocalBoundsOf(region) is not { } bounds) return null;
			var corner = FrameCorner(region.Body, bounds);
			return new Size(bounds.Right - corner.X, bounds.Bottom - corner.Y);
		} finally {
			m_measuring.Remove(callee);
		}
	}

	public LuaGraph Graph { get; }
	public INodeMeasure Measure { get; set; }
	public GraphKeys Keys { get; private set; }

	public Dictionary<GraphNode, Vec> Local { get; } = [];

	public Dictionary<Block, Vec> Origins { get; } = [];

	public HashSet<Block> PinnedRegions { get; } = [];
	public HashSet<Block> CollapsedRegions { get; } = [];
	public HashSet<GraphNode> PinnedNodes { get; } = [];
	public HashSet<GraphNode> CollapsedNodes { get; } = [];
	public List<LayoutGroup> Groups { get; } = [];

	public Dictionary<InputPin, List<Vec>> Reroutes { get; } = [];

	public Dictionary<InputPin, GraphNode> CallableWireHosts { get; } = [];

	public Dictionary<InputPin, Vec> ProxyOffsets { get; } = [];

	public Dictionary<Statement, List<Vec>> ExecReroutes { get; } = [];

	public HashSet<GraphNode> Loose { get; } = [];

	public void Refresh() {
		Keys = GraphKeys.Compute(Graph);
		IndexRegions();
		foreach (var dead in Local.Keys.Where(n => Keys.KeyOf(n) is null && !Loose.Contains(n)).ToList()) Local.Remove(dead);
		Loose.RemoveWhere(n => !Graph.Nodes.Contains(n));
		foreach (var group in Groups) group.Members.RemoveAll(m => Keys.KeyOf(m) is null);
		Groups.RemoveAll(g => g.Members.Count == 0);
		PinnedNodes.RemoveWhere(n => Keys.KeyOf(n) is null);
		foreach (var body in Embedded.Where(e => (!Loose.Contains(e.Value) && Keys.KeyOf(e.Value) is null) || !e.Value.IsExpanded || NodeShape.LocalCalleeOf(e.Value)?.Entry.Body != e.Key || HostBody(e.Value) == e.Key).Select(e => e.Key).ToList()) Embedded.Remove(body);
		foreach (var host in Keys.Ordered.Select(k => k.Node).Concat(Loose).Where(node => node.IsExpanded && !Embedded.ContainsValue(node)).Distinct().ToList()) {
			var region = NodeShape.LocalCalleeOf(host) is { } callee ? Keys.Regions.FirstOrDefault(r => r.Header == callee) : null;
			if (region is not null && !Embedded.ContainsKey(region.Body) && HostBody(host) != region.Body) Embedded[region.Body] = host;
			else host.IsExpanded = false;
		}
		foreach (var dead in Reroutes.Keys.Where(p => p.Source is null || Keys.KeyOf(InlineRules.Drawn(p.Owner)) is null).ToList()) Reroutes.Remove(dead);
		foreach (var dead in CallableWireHosts.Where(pair => Keys.KeyOf(pair.Key.Owner) is null || Keys.KeyOf(pair.Value) is null || CallableSlot(pair.Key, pair.Value) is null).Select(pair => pair.Key).ToList())
			CallableWireHosts.Remove(dead);
		foreach (var dead in ProxyOffsets.Keys.Where(p => p.Source is null || Keys.KeyOf(InlineRules.Drawn(p.Owner)) is null).ToList()) ProxyOffsets.Remove(dead);
		foreach (var dead in ExecReroutes.Keys.Where(s => Keys.KeyOf(s) is null).ToList()) ExecReroutes.Remove(dead);
		CollapsedNodes.RemoveWhere(n => Keys.KeyOf(n) is null);
		var live = Keys.Regions.Select(r => r.Body).ToHashSet();
		foreach (var dead in Origins.Keys.Where(b => !live.Contains(b)).ToList()) Origins.Remove(dead);
		foreach (var dead in FrameCorners.Keys.Where(b => !live.Contains(b)).ToList()) FrameCorners.Remove(dead);
		PinnedRegions.RemoveWhere(b => !live.Contains(b));
		CollapsedRegions.RemoveWhere(b => !live.Contains(b));
	}

	private void IndexRegions() {
		m_nodesOfRegion = Keys.Regions.ToDictionary(r => r, _ => new List<GraphNode>());
		foreach (var (node, _) in Keys.All)
			if (Keys.RegionOf(node) is { } region)
				m_nodesOfRegion[region].Add(node);
	}

	public IReadOnlyList<GraphNode> NodesOf(RegionInfo region) => m_nodesOfRegion.TryGetValue(region, out var list) ? list : [];

	public bool IsPlaced(GraphNode node) => Local.ContainsKey(node);

	public Dictionary<Block, GraphNode> Embedded { get; } = [];
	public Dictionary<Block, Vec> FrameCorners { get; } = [];

	private Vec FrameCorner(Block body, Rect bounds) => FrameCorners.TryGetValue(body, out var corner)
		? new Vec(Math.Min(corner.X, bounds.X), Math.Min(corner.Y, bounds.Y)) : bounds.TopLeft;

	public Vec ConstrainFrameMove(GraphNode node, Vec delta) {
		if (Keys.RegionOf(node) is not { } region || !Embedded.ContainsKey(region.Body) || IsHidden(node) ||
			LocalBoundsOf(region) is not { } bounds) return delta;
		var corner = FrameCorner(region.Body, bounds);
		var at = Local[node];
		return new Vec(Math.Max(delta.X, corner.X - at.X), Math.Max(delta.Y, corner.Y - at.Y));
	}

	public void AnchorFrame(GraphNode node) {
		if (Keys.RegionOf(node) is { } region && Embedded.ContainsKey(region.Body) && LocalBoundsOf(region) is { } bounds)
			FrameCorners[region.Body] = FrameCorner(region.Body, bounds);
	}

	// unwired statements still need a scope for name lookup
	public Block? HostBody(GraphNode host) {
		if (Keys.RegionOf(host) is { } region) return region.Body;
		for (var block = (host as Statement ?? host.OwnerStatement)?.Parent; block is not null; block = (block.Owner as Statement)?.Parent)
			if (block == Graph.Module || block.Owner is FunctionEntry) return block;
		return null;
	}

	public Slot? CallableSlot(InputPin target) => CallableWireHosts.TryGetValue(target, out var host) ? CallableSlot(target, host) : null;

	private static Slot? CallableSlot(InputPin target, GraphNode host) {
		if (target.Source is not { } source) return null;
		return NodeShape.SlotsOf(host).FirstOrDefault(slot => slot is { IsCallableReference: true, Out: { } callable } &&
			(source == callable || NodeShape.BindingOf(source) is { } binding && binding == NodeShape.BindingOf(callable) ||
				source.Owner is MemberGet a && callable.Owner is MemberGet b && a.Name == b.Name &&
				NodeShape.BindingOf(a.Object.Source) is { } receiver && receiver == NodeShape.BindingOf(b.Object.Source)));
	}

	public Vec OriginOf(RegionInfo region) {
		if (Embedded.TryGetValue(region.Body, out var host) && Local.ContainsKey(host) && LocalBoundsOf(region) is { } inside) {
			var inset = DefaultMeasure.FrameInset(host);
			return Position(host) + inset - FrameCorner(region.Body, inside);
		}
		return Origins.GetValueOrDefault(region.Body);
	}

	public Rect? LocalBoundsOf(RegionInfo region) {
		Rect? total = null;
		foreach (var node in NodesOf(region)) {
			if (!Local.ContainsKey(node) || InlineRules.IsMerged(node) || IsHidden(node)) continue;
			var b = BoundsLocal(node);
			total = total is null ? b : Rect.Union(total.Value, b);
		}
		return total;
	}

	public Vec Position(GraphNode node) {
		if (Loose.Contains(node)) return Local.GetValueOrDefault(node);
		var local = Local.GetValueOrDefault(node);
		return Keys.RegionOf(node) is { } region ? OriginOf(region) + local : local;
	}

	public void SetPosition(GraphNode node, Vec canvas) {
		var origin = Keys.RegionOf(node) is { } region ? OriginOf(region) : default;
		Local[node] = canvas - origin;
	}

	public Rect BoundsLocal(GraphNode node) {
		var p = Local.GetValueOrDefault(node);
		var s = Measure.Measure(node);
		return new Rect(p.X, p.Y, s.W, s.H);
	}

	public Rect Bounds(GraphNode node) {
		var p = Position(node);
		var s = Measure.Measure(node);
		return new Rect(p.X, p.Y, s.W, s.H);
	}

	public Rect? RegionBounds(RegionInfo region) {
		Rect? total = null;
		foreach (var node in NodesOf(region)) {
			if (!Local.ContainsKey(node) || InlineRules.IsMerged(node) || IsHidden(node)) continue;
			var b = Bounds(node);
			total = total is null ? b : Rect.Union(total.Value, b);
		}
		return total;
	}

	public void MoveRegion(RegionInfo region, Vec delta) {
		Origins[region.Body] = OriginOf(region) + delta;
		foreach (var group in GroupsOf(region)) group.Rect = group.Rect.Offset(delta);
	}

	public IEnumerable<LayoutGroup> GroupsOf(RegionInfo region) =>
		Groups.Where(g => g.Members.FirstOrDefault(m => Keys.KeyOf(m) is not null) is { } member && Keys.RegionOf(member) == region);

	public Rect? RegionExtent(RegionInfo region) {
		if (RegionBounds(region) is not { } bounds) return null;
		foreach (var group in GroupsOf(region)) bounds = Rect.Union(bounds, group.Rect);
		return bounds;
	}

	public void PlaceRegion(RegionInfo region, Vec topLeft) {
		if (RegionExtent(region) is not { } bounds) return;
		MoveRegion(region, topLeft - bounds.TopLeft);
	}

	public bool IsHidden(GraphNode node) {
		if (Keys.RegionOf(node) is not { } region || !Embedded.ContainsKey(region.Body)) return false;
		if (node == region.Header || node == region.Entry) return true;
		return node is ReturnStatement ret && NodeShape.TrailingReturn(region.Body) == ret;
	}

	public bool IsInsideFrameOf(GraphNode node, GraphNode host) {
		for (var body = HostBody(node); body is not null && Embedded.TryGetValue(body, out var outer); body = HostBody(outer))
			if (outer == host) return true;
		return false;
	}

	public IEnumerable<(GraphNode A, GraphNode B)> Overlaps() {

		var placed = Local.Keys.Where(n => Keys.KeyOf(n) is not null && !InlineRules.IsMerged(n) && !IsHidden(n)).ToList();
		var rects = placed.Select(Bounds).ToList();
		for (var i = 0; i < placed.Count; i++)
			for (var j = i + 1; j < placed.Count; j++)
				if (rects[i].Intersects(rects[j]) && !IsInsideFrameOf(placed[i], placed[j]) && !IsInsideFrameOf(placed[j], placed[i])) yield return (placed[i], placed[j]);
	}
}
