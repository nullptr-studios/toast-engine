using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Layout;

public static class LayoutPersistence {
	public static GraphMeta Capture(GraphLayout layout) {
		var keys = layout.Keys;
		var meta = new GraphMeta();

		foreach (var (node, key) in keys.Ordered) {
			if (!layout.Local.TryGetValue(node, out var local) || InlineRules.IsMerged(node)) continue;
			var pinned = layout.PinnedNodes.Contains(node);
			var collapsed = layout.CollapsedNodes.Contains(node);
			var hash = key.IndexOf('#');
			if (hash >= 0) {
				if (keys.NodeOf(key[..hash]) is { } owner && layout.Local.TryGetValue(owner, out var ownerLocal))
					meta.Nodes.Add(new NodeMeta(key, "", local.X - ownerLocal.X, local.Y - ownerLocal.Y, pinned, collapsed));
				continue;
			}
			meta.Nodes.Add(new NodeMeta(key, node is Statement s and not SequenceStatement ? GraphKeys.Fingerprint(s) : "", local.X, local.Y, pinned, collapsed));
		}

		foreach (var region in keys.Regions) {
			if (layout.RegionBounds(region) is null) continue;
			var origin = layout.Origins.GetValueOrDefault(region.Body);
			meta.Regions.Add(new RegionMeta(region.Key, origin.X, origin.Y, layout.PinnedRegions.Contains(region.Body), layout.CollapsedRegions.Contains(region.Body),
				layout.FrameCorners.TryGetValue(region.Body, out var corner) ? corner : null));
		}

		foreach (var group in layout.Groups) {
			var members = group.Members.Select(keys.KeyOf).OfType<string>().ToList();
			if (members.Count == 0) continue;
			var region = keys.RegionOf(group.Members.First(m => keys.KeyOf(m) is not null));
			var origin = region is null ? default : layout.OriginOf(region);
			meta.Groups.Add(new GroupMeta(group.Title, group.Color, region?.Key ?? "", group.Rect.X - origin.X, group.Rect.Y - origin.Y, group.Rect.W, group.Rect.H, members));
		}

		foreach (var sequence in layout.Graph.AllStatements().OfType<SequenceStatement>()) {
			var key = keys.KeyOf(sequence);
			if (key is null) continue;
			var outputs = new List<string>();
			var counts = new List<int>();
			foreach (var branch in sequence.Branches) {
				var flat = GraphKeys.Flatten(branch).ToList();
				outputs.Add(flat.Count == 0 ? "" : keys.KeyOf(flat[0]) ?? "");
				counts.Add(flat.Count);
			}
			meta.Sequences.Add(new SequenceMeta(key, outputs, counts));
		}
		foreach (var (pin, points) in layout.Reroutes) {
			var drawn = InlineRules.Drawn(pin.Owner);
			if (keys.KeyOf(drawn) is not { } key || NodeShape.SlotOf(pin) is not { } slot || points.Count == 0) continue;
			meta.Reroutes.Add(new RerouteMeta(key, slot.Row, points.SelectMany(p => new[] { p.X, p.Y }).ToList()));
		}
		foreach (var (pin, offset) in layout.ProxyOffsets) {
			var drawn = InlineRules.Drawn(pin.Owner);
			if (keys.KeyOf(drawn) is not { } key || NodeShape.SlotOf(pin) is not { } slot) continue;
			meta.Reroutes.Add(new RerouteMeta(key, ProxyRow + slot.Row, [offset.X, offset.Y]));
		}
		foreach (var (statement, points) in layout.ExecReroutes) {
			if (keys.KeyOf(statement) is not { } key || points.Count == 0) continue;
			meta.Reroutes.Add(new RerouteMeta(key, ExecRow, points.SelectMany(q => new[] { q.X, q.Y }).ToList()));
		}
		foreach (var (pin, host) in layout.CallableWireHosts)
			if (keys.KeyOf(pin.Owner) is { } target && keys.KeyOf(host) is { } hostKey && layout.CallableSlot(pin) is not null)
				meta.CallableWires.Add(new CallableWireMeta(target, pin.Owner.Inputs.ToList().IndexOf(pin), hostKey));
		foreach (var (node, key) in keys.Ordered) {
			if (node is not ExprNode) continue;
			foreach (var output in node.Outputs.Where(o => o.Consumers.Count > 1))
				foreach (var pin in output.Consumers) {
					if (keys.KeyOf(pin.Owner) is not { } target || keys.RegionOf(node) != keys.RegionOf(pin.Owner)) continue;
					var input = pin.Owner.Inputs.ToList().IndexOf(pin);
					// the first occurrence already owns the expression key
					var separator = pin.Owner is Statement ? "#" : ".";
					if (key != target + separator + input)
						meta.SharedWires.Add(new SharedWireMeta(key, output.Index, target, input));
				}
		}
		foreach (var host in layout.Embedded.Values.Distinct())
			if (keys.KeyOf(host) is { } hostKey) meta.Expanded.Add(hostKey);
		foreach (var table in layout.Graph.Nodes.OfType<TableNode>())
			if (table.DeclaredFlavor is { } chosen && keys.KeyOf(table) is { } tableKey) meta.Flavors.Add(new FlavorMeta(tableKey, FlavorText(table)));
		foreach (var statement in layout.Loose.OfType<Statement>().Where(x => x.IsFloating && layout.Local.ContainsKey(x))) {
			var text = Fragments.Copy([statement]).Trim();
			var owner = statement.Parent?.Owner is FunctionEntry { Owner: FunctionDecl declaration } ? declaration.Name : "";
			if (text.Length > 0) meta.Floating.Add(new LooseMeta(text, layout.Local[statement].X, layout.Local[statement].Y, owner, Expanded: statement.IsExpanded));
		}
		foreach (var node in layout.Loose.OfType<ExprNode>()) {
			if (node.Outputs.Any(o => o.Consumers.Count > 0) || !layout.Local.TryGetValue(node, out var at)) continue;
			if (Fragments.CopyExpression(node) is { Length: > 0 } text) meta.Loose.Add(new LooseMeta(text, at.X, at.Y, "", node is TableNode { DeclaredFlavor: not null } looseTable ? FlavorText(looseTable) : ""));
		}
		return meta;
	}

	private const int ExecRow = -1;

	private const int ProxyRow = 1000;

	public static GraphLayout Restore(LuaGraph graph, GraphMeta? meta, INodeMeasure? measure = null) {
		var layout = new GraphLayout(graph, measure ?? new DefaultMeasure());
		var matcher = meta is null ? null : new Matcher(layout, meta);
		matcher?.ApplyPositions();
		matcher?.RestoreSharedWires();
		matcher?.RestoreExpanded();
		matcher?.RebuildSequences();
		LayoutEngine.PlaceUnplaced(layout);
		LayoutEngine.PackRegions(layout);
		matcher?.RestoreGroups();
		matcher?.RestoreReroutes();
		matcher?.RestoreFlavors();
		if (meta is not null) {
			RestoreLoose(layout, meta);
			foreach (var wire in meta.CallableWires) {
				if (layout.Keys.NodeOf(wire.Target) is not { } target || layout.Keys.NodeOf(wire.Host) is not { } host ||
					wire.Input < 0 || wire.Input >= target.Inputs.Count) continue;
				var pin = target.Inputs[wire.Input];
				layout.CallableWireHosts[pin] = host;
				if (layout.CallableSlot(pin) is null) layout.CallableWireHosts.Remove(pin);
			}
		}
		return layout;
	}

	private static void RestoreLoose(GraphLayout layout, GraphMeta meta) {
		var added = new List<GraphNode>();
		var expanded = new List<Statement>();
		foreach (var saved in meta.Floating) {
			var module = saved.Region.Length > 0 && layout.Graph.Module.Statements.OfType<FunctionDecl>().FirstOrDefault(d => d.Name == saved.Region) is { } function ? function.Entry.Body : layout.Graph.Module;
			var count = module.Statements.Count;
			var index = count > 0 && module.Statements[^1] is ReturnStatement ? count - 1 : count;
			var result = Fragments.Paste(layout.Graph, saved.Expression + "\n", module, index);
			if (result.Errors.Count > 0) continue;
			foreach (var statement in result.Statements) {
				statement.IsFloating = true;
				layout.Loose.Add(statement);
				layout.Local[statement] = new Vec(saved.X, saved.Y);
				added.Add(statement);
				if (saved.Expanded) expanded.Add(statement);
			}
		}
		if (added.Count > 0) layout.Refresh();

		foreach (var saved in meta.Loose) {
			var nodes = Fragments.BuildLoose(layout.Graph, saved.Expression);
			if (nodes.Count == 0) continue;
			if (nodes[0] is TableNode madeAs) ApplyFlavor(madeAs, saved.Flavor);
			var depth = new Dictionary<ExprNode, int> { [nodes[0]] = 0 };
			var perColumn = new Dictionary<int, int>();
			foreach (var node in nodes) {
				if (!depth.TryGetValue(node, out var d)) continue;
				foreach (var input in node.Inputs)
					if (input.Source?.Owner is ExprNode child && nodes.Contains(child) && !depth.ContainsKey(child)) depth[child] = d + 1;
			}
			foreach (var node in nodes) {
				var d = depth.GetValueOrDefault(node, 1);
				var row = perColumn.GetValueOrDefault(d);
				perColumn[d] = row + 1;
				layout.Loose.Add(node);
				layout.Local[node] = new Vec(saved.X - d * 260, saved.Y + row * 90);
				added.Add(node);
			}
		}
		if (added.Count == 0) return;
		layout.Graph.Analyze();
		foreach (var host in expanded) {
			if (NodeShape.LocalCalleeOf(host)?.Entry.Body is not { } body ||
				layout.HostBody(host) is not { } own || own == body ||
				layout.Embedded.ContainsKey(own) || layout.Embedded.ContainsKey(body) ||
				layout.Embedded.Values.Any(other => layout.HostBody(other) == body)) continue;
			host.IsExpanded = true;
			layout.Embedded[body] = host;
			layout.CollapsedRegions.Remove(body);
		}
		layout.Refresh();
		LayoutEngine.Separate(layout, added);
	}

	private static string FlavorText(TableNode table) => table.DeclaredFlavor + (table.DeclaredType is { } type ? "|" + TypeFormatter.Format(type) : "");

	internal static void ApplyFlavor(TableNode table, string text) {
		var parts = text.Split('|', 2);
		if (Enum.TryParse<TableFlavor>(parts[0], out var flavor)) table.DeclaredFlavor = flavor;
		if (parts.Length == 2) {
			try {
				table.DeclaredType = TypeExprParser.Parse(parts[1]);
			} catch (TypeSyntaxException) {
			}
		}
	}

	private sealed class Matcher {

		private static readonly Regex s_closureEntry = new(@"^(.*)(/fn\d+)$", RegexOptions.Compiled);

		private readonly GraphLayout m_layout;
		private readonly GraphMeta m_meta;
		private GraphKeys m_keys => m_layout.Keys;
		private readonly Dictionary<string, NodeMeta> m_saved = [];
		private readonly Dictionary<string, List<NodeMeta>> m_children = [];
		private readonly Dictionary<Statement, string> m_map = [];
		private readonly Dictionary<string, Statement> m_inverse = [];
		private readonly Dictionary<string, string> m_newToSaved = [];

		public Matcher(GraphLayout layout, GraphMeta meta) {
			m_layout = layout;
			m_meta = meta;
			foreach (var n in meta.Nodes) {
				m_saved[n.Key] = n;
				if (n.Fingerprint.Length == 0 || n.Key.Contains('#') || n.Key.Contains('!') || n.Key.StartsWith('~')) continue;
				var slash = n.Key.LastIndexOf('/');
				var parent = slash < 0 ? "" : n.Key[..slash];
				if (!m_children.TryGetValue(parent, out var list)) m_children[parent] = list = [];
				list.Add(n);
			}
			AlignBlock(layout.Graph.Module, "");
			foreach (var (statement, saved) in m_map)
				if (m_keys.KeyOf(statement) is { } newKey) m_newToSaved[newKey] = saved;
		}

		private void AlignBlock(Block block, string parent) {
			var saved = m_children.GetValueOrDefault(parent) ?? [];
			var fresh = GraphKeys.Flatten(block).ToList();
			foreach (var (s, n) in AlignLists(saved.Select(x => x.Fingerprint).ToList(), fresh.Select(GraphKeys.Fingerprint).ToList()))
				Matched(fresh[n], saved[s].Key);
		}

		private void Matched(Statement statement, string savedKey) {
			m_map[statement] = savedKey;
			m_inverse[savedKey] = statement;
			if (statement is FunctionDecl decl) AlignBlock(decl.Entry.Body, savedKey);
			else
				foreach (var child in statement.ChildBlocks)
					AlignBlock(child, savedKey + "/" + GraphKeys.RoleOf(statement, child));

			var key = m_keys.KeyOf(statement);
			if (key is null) return;
			foreach (var region in m_keys.Regions)
				if (region.Header is FunctionNode && region.Key.StartsWith(key + "/fn", StringComparison.Ordinal) && region.Key.IndexOf("/fn", key.Length + 1, StringComparison.Ordinal) < 0)
					AlignBlock(region.Body, savedKey + region.Key[key.Length..]);
		}

		internal static List<(int Saved, int Fresh)> AlignLists(IReadOnlyList<string> saved, IReadOnlyList<string> fresh) {
			var anchors = new List<(int, int)>();
			var prefix = 0;
			while (prefix < saved.Count && prefix < fresh.Count && saved[prefix] == fresh[prefix]) {
				anchors.Add((prefix, prefix));
				prefix++;
			}
			var endS = saved.Count;
			var endF = fresh.Count;
			var tail = new List<(int, int)>();
			while (endS > prefix && endF > prefix && saved[endS - 1] == fresh[endF - 1]) {
				endS--;
				endF--;
				tail.Add((endS, endF));
			}

			var rows = endS - prefix;
			var cols = endF - prefix;
			if (rows > 0 && cols > 0 && (long)rows * cols <= 4_000_000) {
				var table = new int[rows + 1, cols + 1];
				for (var i = rows - 1; i >= 0; i--)
					for (var j = cols - 1; j >= 0; j--)
						table[i, j] = saved[prefix + i] == fresh[prefix + j] ? table[i + 1, j + 1] + 1 : Math.Max(table[i + 1, j], table[i, j + 1]);
				int a = 0, b = 0;
				while (a < rows && b < cols) {
					if (saved[prefix + a] == fresh[prefix + b]) {
						anchors.Add((prefix + a, prefix + b));
						a++;
						b++;
					} else if (table[a + 1, b] >= table[a, b + 1]) {
						a++;
					} else {
						b++;
					}
				}
			}
			tail.Reverse();
			anchors.AddRange(tail);

			var result = new List<(int, int)>();
			int prevS = -1, prevF = -1;
			foreach (var (s, f) in anchors.Append((saved.Count, fresh.Count))) {
				var gap = Math.Min(s - prevS - 1, f - prevF - 1);
				for (var k = 1; k <= gap; k++) result.Add((prevS + k, prevF + k));
				if (s < saved.Count) result.Add((s, f));
				prevS = s;
				prevF = f;
			}
			return result;
		}

		private string? SavedKeyOf(string key) {
			if (key.StartsWith('~')) return null;
			string statement;
			string suffix;
			var hash = key.IndexOf('#');
			if (hash >= 0) {
				statement = key[..hash];
				suffix = key[hash..];
			} else if (key.EndsWith("!entry", StringComparison.Ordinal)) {
				var body = key[..^"!entry".Length];
				var closure = s_closureEntry.Match(body);
				if (closure.Success) {
					statement = closure.Groups[1].Value;
					suffix = closure.Groups[2].Value + "!entry";
				} else {
					statement = body;
					suffix = "!entry";
				}
			} else {
				statement = key;
				suffix = "";
			}
			return m_newToSaved.TryGetValue(statement, out var saved) ? saved + suffix : null;
		}

		public void ApplyPositions() {
			var deferred = new List<(GraphNode Node, NodeMeta Meta, string OwnerKey)>();
			foreach (var (node, key) in m_keys.Ordered) {
				var savedKey = SavedKeyOf(key);
				if (savedKey is null || !m_saved.TryGetValue(savedKey, out var entry)) continue;
				var hash = savedKey.IndexOf('#');
				if (hash >= 0) {
					deferred.Add((node, entry, key[..key.IndexOf('#')]));
					continue;
				}
				m_layout.Local[node] = new Vec(entry.X, entry.Y);
				Flags(node, entry);
			}
			foreach (var (node, entry, ownerKey) in deferred) {
				if (m_keys.NodeOf(ownerKey) is not { } owner || !m_layout.Local.TryGetValue(owner, out var ownerLocal)) continue;
				m_layout.Local[node] = ownerLocal + new Vec(entry.X, entry.Y);
				Flags(node, entry);
			}

			foreach (var region in m_keys.Regions) {
				var savedKey = region.IsModule ? GraphKeys.ModuleKey
					: region.Header is FunctionDecl decl ? m_map.GetValueOrDefault(decl)
					: ClosureRegionKey(region.Key);
				if (savedKey is null) continue;
				var saved = m_meta.Regions.FirstOrDefault(r => r.Function == savedKey);
				if (saved is null) continue;
				m_layout.Origins[region.Body] = new Vec(saved.X, saved.Y);
				if (saved.FrameCorner is { } corner) m_layout.FrameCorners[region.Body] = corner;
				if (saved.Pinned) m_layout.PinnedRegions.Add(region.Body);
				if (saved.Collapsed) m_layout.CollapsedRegions.Add(region.Body);
			}
		}

		private string? ClosureRegionKey(string key) {
			var m = s_closureEntry.Match(key);
			return m.Success && m_newToSaved.TryGetValue(m.Groups[1].Value, out var saved) ? saved + m.Groups[2].Value : null;
		}

		private void Flags(GraphNode node, NodeMeta entry) {
			if (entry.Pinned) m_layout.PinnedNodes.Add(node);
			if (entry.Collapsed) m_layout.CollapsedNodes.Add(node);
		}

		public void RebuildSequences() {
			var created = false;
			foreach (var saved in m_meta.Sequences) {
				if (saved.Outputs.Count == 0 || saved.Outputs.Count != saved.Counts.Count || saved.Outputs.Any(o => o.Length == 0 || !m_inverse.ContainsKey(o))) continue;
				var firsts = saved.Outputs.Select(o => m_inverse[o]).ToList();
				var block = firsts[0].Parent;
				if (block is null || firsts.Any(f => f.Parent != block)) continue;
				var starts = firsts.Select(block.IndexOf).ToList();
				var ok = true;
				for (var i = 0; i < starts.Count; i++) {
					if (starts[i] < 0 || (i > 0 && starts[i] <= starts[i - 1])) ok = false;
					else if (i + 1 < starts.Count && starts[i + 1] - starts[i] != saved.Counts[i]) ok = false;
					else if (i + 1 == starts.Count && starts[i] + saved.Counts[i] != block.Statements.Count) ok = false;
				}
				if (!ok) continue;

				SequenceStatement sequence;
				try {
					sequence = m_layout.Graph.GroupAsSequence(block, starts);
				} catch (ArgumentException) {
					continue;
				}
				created = true;
				if (m_saved.TryGetValue(saved.Key, out var entry)) {
					m_layout.Local[sequence] = new Vec(entry.X, entry.Y);
					Flags(sequence, entry);
				}
			}
			if (created) m_layout.Refresh();
		}

		public void RestoreExpanded() {
			foreach (var key in m_meta.Expanded) {
				if (Resolve(key) is not { } host || NodeShape.LocalCalleeOf(host) is not { } callee) continue;
				if (m_keys.Regions.FirstOrDefault(r => r.Header == callee) is not { } region || m_layout.Embedded.ContainsKey(region.Body) || m_keys.RegionOf(host) == region) continue;
				var hostRegion = m_keys.RegionOf(host);
				if (hostRegion is not null && m_layout.Embedded.ContainsKey(hostRegion.Body) ||
					m_layout.Embedded.Values.Any(existing => m_keys.RegionOf(existing) == region)) continue;
				m_layout.CollapsedRegions.Remove(region.Body);
				host.IsExpanded = true;
				m_layout.Embedded[region.Body] = host;
			}
		}

		public void RestoreFlavors() {
			foreach (var saved in m_meta.Flavors)
				if (Resolve(saved.Key) is TableNode table) ApplyFlavor(table, saved.Flavor);
		}

		public void RestoreSharedWires() {
			// folding duplicates changes the paths used to restore wires
			var links = new List<(OutputPin Source, InputPin Target)>();
			foreach (var wire in m_meta.SharedWires) {
				if (Resolve(wire.Source) is not ExprNode source || Resolve(wire.Target) is not { } target ||
					wire.Output < 0 || wire.Output >= source.Outputs.Count || wire.Input < 0 || wire.Input >= target.Inputs.Count ||
					m_keys.RegionOf(source) != m_keys.RegionOf(target)) continue;
				links.Add((source.Outputs[wire.Output], target.Inputs[wire.Input]));
			}
			var changed = false;
			foreach (var (source, target) in links)
				changed |= m_layout.Graph.TryRestoreSharedValue(source, target);
			if (changed) m_layout.Refresh();
		}

		public void RestoreReroutes() {
			foreach (var saved in m_meta.Reroutes) {
				if (Resolve(saved.Key) is not { } node || saved.Points.Count % 2 != 0) continue;
				if (saved.Row >= ProxyRow) {
					var feeds = NodeShape.SlotsOf(InlineRules.Drawn(node)).FirstOrDefault(s => s.Kind == SlotKind.DataIn && s.Row == saved.Row - ProxyRow);
					if (feeds?.In is { Source: not null } proxied && saved.Points.Count == 2) m_layout.ProxyOffsets[proxied] = new Vec(saved.Points[0], saved.Points[1]);
					continue;
				}
				if (saved.Row == ExecRow) {
					if (node is Statement target) m_layout.ExecReroutes[target] = Pairs(saved.Points);
					continue;
				}
				var slot = NodeShape.SlotsOf(InlineRules.Drawn(node)).FirstOrDefault(s => s.Kind == SlotKind.DataIn && s.Row == saved.Row);
				if (slot?.In is not { Source: not null } pin) continue;
				m_layout.Reroutes[pin] = Pairs(saved.Points);
			}
		}

		private static List<Vec> Pairs(IReadOnlyList<double> numbers) {
			var list = new List<Vec>();
			for (var i = 0; i < numbers.Count; i += 2) list.Add(new Vec(numbers[i], numbers[i + 1]));
			return list;
		}

		public void RestoreGroups() {
			foreach (var saved in m_meta.Groups) {
				var members = new List<GraphNode>();
				foreach (var key in saved.Members)
					if (Resolve(key) is { } node && m_layout.Local.ContainsKey(node)) members.Add(node);
				if (members.Count == 0) continue;

				var group = new LayoutGroup { Title = saved.Title, Color = saved.Color };
				group.Members.AddRange(members);
				if (members.Count == saved.Members.Count) {
					var origin = m_keys.RegionOf(members[0]) is { } region ? m_layout.OriginOf(region) : default;
					group.Rect = new Rect(saved.X + origin.X, saved.Y + origin.Y, saved.W, saved.H);
				} else {
					group.Rect = Around(members);
				}
				m_layout.Groups.Add(group);
			}
		}

		private Rect Around(IReadOnlyList<GraphNode> members) => AroundCore(members.Select(m_layout.Bounds));

		private static Rect AroundCore(IEnumerable<Rect> rects) {
			var union = rects.Aggregate(Rect.Union);
			return new Rect(union.X - 16, union.Y - 36, union.W + 32, union.H + 52);
		}

		private GraphNode? Resolve(string savedKey) {
			var hash = savedKey.IndexOf('#');
			var statement = hash >= 0 ? savedKey[..hash] : savedKey;
			var suffix = hash >= 0 ? savedKey[hash..] : "";
			if (savedKey.StartsWith('~')) return m_layout.Keys.NodeOf(savedKey);
			if (!m_inverse.TryGetValue(statement, out var node) || m_layout.Keys.KeyOf(node) is not { } newKey) return null;
			return m_layout.Keys.NodeOf(newKey + suffix);
		}

	}
}
