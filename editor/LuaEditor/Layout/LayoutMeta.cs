using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Tomlyn;
using Tomlyn.Model;

namespace editor.LuaEditor.Layout;

public sealed record NodeMeta(string Key, string Fingerprint, double X, double Y, bool Pinned, bool Collapsed);

public sealed record RegionMeta(string Function, double X, double Y, bool Pinned, bool Collapsed, Vec? FrameCorner = null);

public sealed record GroupMeta(string Title, string Color, string Region, double X, double Y, double W, double H, IReadOnlyList<string> Members);

public sealed record SequenceMeta(string Key, IReadOnlyList<string> Outputs, IReadOnlyList<int> Counts);

public sealed record CallableWireMeta(string Target, int Input, string Host);

public sealed record SharedWireMeta(string Source, int Output, string Target, int Input);

public sealed record RerouteMeta(string Key, int Row, IReadOnlyList<double> Points);

public sealed record LooseMeta(string Expression, double X, double Y, string Region = "", string Flavor = "", bool Expanded = false);

public sealed record FlavorMeta(string Key, string Flavor);

public sealed class GraphMeta {
	public const int CurrentVersion = 1;

	public List<NodeMeta> Nodes { get; } = [];
	public List<RegionMeta> Regions { get; } = [];
	public List<GroupMeta> Groups { get; } = [];
	public List<SequenceMeta> Sequences { get; } = [];
	public List<RerouteMeta> Reroutes { get; } = [];
	public List<CallableWireMeta> CallableWires { get; } = [];
	public List<SharedWireMeta> SharedWires { get; } = [];
	public List<LooseMeta> Loose { get; } = [];

	public List<LooseMeta> Floating { get; } = [];

	public List<FlavorMeta> Flavors { get; } = [];

	public List<string> Expanded { get; } = [];

	public static GraphMeta? Read(string metaText) {
		try {
			var root = TomlSerializer.Deserialize<TomlTable>(metaText);
			if (root is null || !root.TryGetValue("graph", out var g) || g is not TomlTable graph) return null;
			if (Int(graph, "version", 0) != CurrentVersion) return null;

			var meta = new GraphMeta();
			foreach (var t in Tables(graph, "nodes")) {
				var pos = Numbers(t, "pos");
				meta.Nodes.Add(new NodeMeta(Str(t, "key"), Str(t, "fp"), pos[0], pos[1], Bool(t, "pinned"), Bool(t, "collapsed")));
			}
			foreach (var t in Tables(graph, "regions")) {
				var pos = Numbers(t, "pos");
				var corner = t.ContainsKey("frame_corner") ? Numbers(t, "frame_corner") : null;
				meta.Regions.Add(new RegionMeta(Str(t, "function"), pos[0], pos[1], Bool(t, "pinned"), Bool(t, "collapsed"),
					corner is null ? null : new Vec(corner[0], corner[1])));
			}
			foreach (var t in Tables(graph, "groups")) {
				var rect = Numbers(t, "rect", 4);
				meta.Groups.Add(new GroupMeta(Str(t, "title"), Str(t, "color"), Str(t, "region"), rect[0], rect[1], rect[2], rect[3], Strings(t, "members")));
			}
			foreach (var t in Tables(graph, "sequences"))
				meta.Sequences.Add(new SequenceMeta(Str(t, "key"), Strings(t, "outputs"), Numbers(t, "counts", -1).Select(d => (int)d).ToList()));
			foreach (var t in Tables(graph, "reroutes"))
				meta.Reroutes.Add(new RerouteMeta(Str(t, "key"), Int(t, "row", 0), Numbers(t, "points", -1)));
			foreach (var t in Tables(graph, "floating"))
				meta.Floating.Add(new LooseMeta(Str(t, "expr"), Numbers(t, "pos")[0], Numbers(t, "pos")[1], Str(t, "region"), Expanded: Bool(t, "expanded")));
			foreach (var t in Tables(graph, "callable_wires"))
				meta.CallableWires.Add(new CallableWireMeta(Str(t, "target"), Int(t, "input", -1), Str(t, "host")));
			foreach (var t in Tables(graph, "shared_wires"))
				meta.SharedWires.Add(new SharedWireMeta(Str(t, "source"), Int(t, "output", -1), Str(t, "target"), Int(t, "input", -1)));
			meta.Expanded.AddRange(Strings(graph, "expanded"));
			foreach (var t in Tables(graph, "flavors"))
				meta.Flavors.Add(new FlavorMeta(Str(t, "key"), Str(t, "kind")));
			foreach (var t in Tables(graph, "loose"))
				meta.Loose.Add(new LooseMeta(Str(t, "expr"), Numbers(t, "pos")[0], Numbers(t, "pos")[1], "", Str(t, "kind")));
			return meta;
		} catch (Exception e) when (e is TomlException or InvalidCastException or KeyNotFoundException or ArgumentException or IndexOutOfRangeException or FormatException) {
			return null;
		}
	}

	public string? Write(string metaText) {
		TomlTable root;
		try {
			root = string.IsNullOrWhiteSpace(metaText) ? new TomlTable() : TomlSerializer.Deserialize<TomlTable>(metaText) ?? new TomlTable();
		} catch (TomlException) {
			return null;
		}

		var graph = new TomlTable { ["version"] = (long)CurrentVersion };
		var nodes = new TomlTableArray();
		foreach (var n in Nodes) {
			var t = new TomlTable { ["key"] = n.Key };
			if (n.Fingerprint.Length > 0) t["fp"] = n.Fingerprint;
			t["pos"] = Pair(n.X, n.Y);
			if (n.Pinned) t["pinned"] = true;
			if (n.Collapsed) t["collapsed"] = true;
			nodes.Add(t);
		}
		graph["nodes"] = nodes;

		var regions = new TomlTableArray();
		foreach (var r in Regions) {
			var t = new TomlTable { ["function"] = r.Function, ["pos"] = Pair(r.X, r.Y) };
			if (r.Pinned) t["pinned"] = true;
			if (r.Collapsed) t["collapsed"] = true;
			if (r.FrameCorner is { } corner) t["frame_corner"] = Pair(corner.X, corner.Y);
			regions.Add(t);
		}
		graph["regions"] = regions;

		var groups = new TomlTableArray();
		foreach (var gr in Groups) {
			var members = new TomlArray();
			foreach (var m in gr.Members) members.Add(m);
			var rect = new TomlArray { Round(gr.X), Round(gr.Y), Round(gr.W), Round(gr.H) };
			groups.Add(new TomlTable { ["title"] = gr.Title, ["color"] = gr.Color, ["region"] = gr.Region, ["rect"] = rect, ["members"] = members });
		}
		graph["groups"] = groups;

		var sequences = new TomlTableArray();
		foreach (var s in Sequences) {
			var outputs = new TomlArray();
			foreach (var o in s.Outputs) outputs.Add(o);
			var counts = new TomlArray();
			foreach (var c in s.Counts) counts.Add((long)c);
			sequences.Add(new TomlTable { ["key"] = s.Key, ["outputs"] = outputs, ["counts"] = counts });
		}
		graph["sequences"] = sequences;

		var reroutes = new TomlTableArray();
		foreach (var r in Reroutes) {
			var points = new TomlArray();
			foreach (var p in r.Points) points.Add(Round(p));
			reroutes.Add(new TomlTable { ["key"] = r.Key, ["row"] = (long)r.Row, ["points"] = points });
		}
		graph["reroutes"] = reroutes;
		if (CallableWires.Count > 0) {
			var wires = new TomlTableArray();
			foreach (var wire in CallableWires)
				wires.Add(new TomlTable { ["target"] = wire.Target, ["input"] = (long)wire.Input, ["host"] = wire.Host });
			graph["callable_wires"] = wires;
		}
		if (SharedWires.Count > 0) {
			var wires = new TomlTableArray();
			foreach (var wire in SharedWires)
				wires.Add(new TomlTable {
					["source"] = wire.Source, ["output"] = (long)wire.Output,
					["target"] = wire.Target, ["input"] = (long)wire.Input
				});
			graph["shared_wires"] = wires;
		}

		var loose = new TomlTableArray();
		foreach (var l in Loose) {
			var lt = new TomlTable { ["expr"] = l.Expression, ["pos"] = Pair(l.X, l.Y) };
			if (l.Flavor.Length > 0) lt["kind"] = l.Flavor;
			loose.Add(lt);
		}

		var flavors = new TomlTableArray();
		foreach (var f in Flavors) flavors.Add(new TomlTable { ["key"] = f.Key, ["kind"] = f.Flavor });
		graph["flavors"] = flavors;

		var expanded = new TomlArray();
		foreach (var key in Expanded) expanded.Add(key);
		graph["expanded"] = expanded;
		graph["loose"] = loose;

		var floating = new TomlTableArray();
		foreach (var f in Floating) {
			var t = new TomlTable { ["expr"] = f.Expression, ["pos"] = Pair(f.X, f.Y) };
			if (f.Region.Length > 0) t["region"] = f.Region;
			if (f.Expanded) t["expanded"] = true;
			floating.Add(t);
		}
		graph["floating"] = floating;

		var section = TomlSerializer.Serialize(new TomlTable { ["graph"] = graph });
		if (Splice(metaText, section) is { } spliced) return spliced;
		root["graph"] = graph;
		return TomlSerializer.Serialize(root);
	}

	private static readonly System.Text.RegularExpressions.Regex s_header = new(@"^\s*\[\[?\s*([A-Za-z0-9_\-\.""]+)\s*\]\]?\s*(#.*)?$", System.Text.RegularExpressions.RegexOptions.Compiled);

	private static string? Splice(string original, string section) {
		var eol = original.Contains("\r\n") ? "\r\n" : "\n";
		var lines = original.Split('\n').Select(l => l.TrimEnd('\r')).ToList();
		if (lines.Count > 0 && lines[^1].Length == 0) lines.RemoveAt(lines.Count - 1);
		var kept = new List<string>();
		var insertAt = -1;
		var inGraph = false;
		foreach (var line in lines) {
			var header = s_header.Match(line);
			if (header.Success) {
				var name = header.Groups[1].Value;
				inGraph = name == "graph" || name.StartsWith("graph.", StringComparison.Ordinal);
				if (inGraph && insertAt < 0) insertAt = kept.Count;
			}
			if (!inGraph) kept.Add(line);
		}
		if (insertAt < 0) {
			if (kept.Count > 0 && kept[^1].Trim().Length > 0) kept.Add("");
			insertAt = kept.Count;
		}
		var block = section.Replace("\r\n", "\n").TrimEnd('\n').Split('\n').ToList();
		block.Add("");
		kept.InsertRange(insertAt, block);
		while (kept.Count > 0 && kept[^1].Length == 0) kept.RemoveAt(kept.Count - 1);
		var result = string.Join(eol, kept) + eol;
		try {
			var table = TomlSerializer.Deserialize<TomlTable>(result);
			return table is not null && table.ContainsKey("graph") ? result : null;
		} catch (TomlException) {
			return null;
		}
	}

	private static double Round(double d) => Math.Round(d, 1);

	private static TomlArray Pair(double x, double y) => new() { Round(x), Round(y) };

	private static IEnumerable<TomlTable> Tables(TomlTable table, string key) {
		if (!table.TryGetValue(key, out var v)) return [];
		if (v is TomlTableArray tables) return tables;
		if (v is TomlArray array) return array.OfType<TomlTable>();
		return [];
	}

	private static string Str(TomlTable t, string key) => t.TryGetValue(key, out var v) && v is string s ? s : "";

	private static bool Bool(TomlTable t, string key) => t.TryGetValue(key, out var v) && v is true;

	private static int Int(TomlTable t, string key, int fallback) => t.TryGetValue(key, out var v) && v is long l ? (int)l : fallback;

	private static List<double> Numbers(TomlTable t, string key, int expected = 2) {
		var list = new List<double>();
		if (t.TryGetValue(key, out var v) && v is TomlArray array)
			foreach (var item in array)
				list.Add(item switch { long l => l, double d => d, _ => throw new FormatException(key) });
		if (expected >= 0 && list.Count != expected) throw new FormatException(key);
		return list;
	}

	private static List<string> Strings(TomlTable t, string key) {
		var list = new List<string>();
		if (t.TryGetValue(key, out var v) && v is TomlArray array)
			foreach (var item in array)
				if (item is string s) list.Add(s);
		return list;
	}
}
