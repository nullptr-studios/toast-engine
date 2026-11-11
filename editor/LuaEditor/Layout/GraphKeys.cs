using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Graph;

namespace editor.LuaEditor.Layout;

public sealed class RegionInfo {
	internal RegionInfo(string key, GraphNode? header, Block body, GraphNode? entry) {
		Key = key;
		Header = header;
		Body = body;
		Entry = entry;
	}

	public string Key { get; }

	public GraphNode? Header { get; }

	public GraphNode? Entry { get; }
	public Block Body { get; }
	public string Name => Header is FunctionDecl d ? d.Name : Key;
	public bool IsModule => Header is null;
}

public sealed class GraphKeys {
	public const string ModuleKey = "<module>";

	private readonly Dictionary<GraphNode, string> m_keys = [];
	private readonly Dictionary<string, GraphNode> m_nodes = [];
	private readonly Dictionary<GraphNode, RegionInfo> m_regionOf = [];
	private readonly List<RegionInfo> m_regions = [];
	private readonly HashSet<GraphNode> m_claimed = [];
	private readonly List<(GraphNode Node, string Key)> m_ordered = [];

	public IReadOnlyList<RegionInfo> Regions => m_regions;

	public static GraphKeys Compute(LuaGraph graph) {
		var keys = new GraphKeys();
		var module = new RegionInfo(ModuleKey, null, graph.Module, null);
		keys.m_regions.Add(module);
		keys.WalkBlock(graph.Module, "", module, true);
		return keys;
	}

	public string? KeyOf(GraphNode node) => m_keys.GetValueOrDefault(node);

	public GraphNode? NodeOf(string key) => m_nodes.GetValueOrDefault(key);

	public RegionInfo? RegionOf(GraphNode node) => m_regionOf.GetValueOrDefault(node);

	public IEnumerable<KeyValuePair<GraphNode, string>> All => m_keys;

	public IReadOnlyList<(GraphNode Node, string Key)> Ordered => m_ordered;

	public static IEnumerable<Statement> Flatten(Block block) {
		foreach (var s in block.Statements) {
			if (s is SequenceStatement sequence) {
				foreach (var branch in sequence.Branches)
					foreach (var inner in Flatten(branch))
						yield return inner;
			} else {
				yield return s;
			}
		}
	}

	public static string Fingerprint(Statement s) => s switch {
		CallStatement c => "call:" + CallName(c.CallNode),
		LocalDecl l => "local:" + string.Join(",", l.Names.Select(n => n.Name)),
		AssignStatement a => "assign:" + string.Join(",", a.Targets.Select(t => t.Name ?? "[]")),
		IfStatement => "if",
		WhileStatement => "while",
		RepeatStatement => "repeat",
		NumericFor n => "for:" + n.Var.Name,
		GenericFor g => "forin:" + string.Join(",", g.Vars.Select(v => v.Name)),
		DoStatement => "do",
		FunctionDecl d => "fn:" + string.Join(d.IsMethod ? ":" : ".", d.Path),
		ReturnStatement => "return",
		BreakStatement => "break",
		GotoStatement g => "goto:" + g.Label,
		LabelStatement l => "label:" + l.Name,
		CastStatement c => "cast:" + c.Variable,
		CommentStatement c => "comment:" + (c.Lines.Count > 0 ? c.Lines[0].Trim() : ""),
		SequenceStatement => "sequence",
		RawStatement r => "raw:" + (r.Text.Length > 24 ? r.Text[..24] : r.Text),
		_ => s.GetType().Name
	};

	private static string CallName(CallNode? call) {
		if (call is null) return "?";
		if (call.Style == CallStyle.Method) return Describe(call.Target.Source?.Owner) + ":" + call.Method;
		return Describe(call.Target.Source?.Owner);
	}

	private static string Describe(GraphNode? node) => node switch {
		GlobalGet g => g.Name,
		VariableGet v => v.Binding.Name,
		MemberGet m => Describe(m.Object.Source?.Owner) + "." + m.Name,
		CallNode c => CallName(c) + "()",
		null => "",
		_ => node.Outputs.Count > 0 && node.Outputs[0].Binding is { } b ? b.Name : node.GetType().Name
	};

	public static string RoleOf(Statement owner, Block block) {
		if (owner is IfStatement i) {
			var index = i.Clauses.FindIndex(c => c.Body == block);
			if (index == 0) return "then";
			if (index > 0) return "elseif" + (index + 1);
			return "else";
		}
		return "body";
	}

	private void WalkBlock(Block block, string prefix, RegionInfo region, bool module) {
		var n = 0;
		foreach (var s in block.Statements) {
			if (s.IsFloating) continue;
			if (s is SequenceStatement sequence) {
				var firstIndex = n + 1;
				WalkSequence(sequence, prefix, region, module, ref n);
				Add(sequence, "~" + prefix + firstIndex, region);
				continue;
			}
			n++;
			WalkStatement(s, prefix + n, region, module);
		}
	}

	private void WalkSequence(SequenceStatement sequence, string prefix, RegionInfo region, bool module, ref int n) {
		foreach (var branch in sequence.Branches) {
			foreach (var s in branch.Statements) {
				if (s is SequenceStatement inner) {
					var firstIndex = n + 1;
					WalkSequence(inner, prefix, region, module, ref n);
					Add(inner, "~" + prefix + firstIndex, region);
					continue;
				}
				n++;
				WalkStatement(s, prefix + n, region, module);
			}
		}
	}

	private void WalkStatement(Statement s, string key, RegionInfo region, bool module) {
		if (s is FunctionDecl decl) {
			var regionKey = !module ? key : decl.IsLocal ? "local " + decl.Name : string.Join(decl.IsMethod ? ":" : ".", decl.Path);
			if (m_nodes.ContainsKey(regionKey) || regionKey == ModuleKey) regionKey = key;
			var own = new RegionInfo(regionKey, decl, decl.Entry.Body, decl.Entry);
			m_regions.Add(own);
			Add(decl, regionKey, own);
			Add(decl.Entry, regionKey + "!entry", own);
			ExpressionKeys(decl, regionKey, own);
			WalkBlock(decl.Entry.Body, regionKey + "/", own, false);
			return;
		}

		Add(s, key, region);
		ExpressionKeys(s, key, region);
		foreach (var child in s.ChildBlocks) WalkBlock(child, key + "/" + RoleOf(s, child) + "/", region, false);
	}

	private void ExpressionKeys(Statement s, string key, RegionInfo region) {
		var closures = 0;
		for (var i = 0; i < s.Inputs.Count; i++) Expression(s.Inputs[i], key + "#" + i, key, region, ref closures);
	}

	private void Expression(InputPin pin, string path, string statementKey, RegionInfo region, ref int closures) {
		if (pin.Source?.Owner is not ExprNode e || !m_claimed.Add(e)) return;
		Add(e, path, region);
		if (e is FunctionNode f) {
			closures++;
			var regionKey = statementKey + "/fn" + closures;
			var own = new RegionInfo(regionKey, f, f.Entry.Body, f.Entry);
			m_regions.Add(own);
			Add(f.Entry, regionKey + "!entry", own);
			WalkBlock(f.Entry.Body, regionKey + "/", own, false);
			return;
		}
		for (var i = 0; i < e.Inputs.Count; i++) Expression(e.Inputs[i], path + "." + i, statementKey, region, ref closures);
	}

	private void Add(GraphNode node, string key, RegionInfo region) {
		if (m_nodes.ContainsKey(key)) key += "'" + m_nodes.Count;
		m_keys[node] = key;
		m_ordered.Add((node, key));
		m_nodes[key] = node;
		m_regionOf[node] = region;
	}
}
