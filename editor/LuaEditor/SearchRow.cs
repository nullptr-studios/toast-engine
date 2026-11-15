using System;
using System.Collections.Generic;
using System.Linq;
using Lucide.Avalonia;
using editor.LuaEditor.Editing;
using editor.LuaEditor.Graph;

namespace editor.LuaEditor;

public sealed class SearchRow {
	public string Text { get; private init; } = "";
	public bool IsGroup { get; private init; }
	public bool IsOpen { get; private init; }
	public int Depth { get; private init; }
	public string Key { get; private init; } = "";
	public NodeEntry? Entry { get; private init; }
	public LucideIconKind Icon { get; private init; }
	public string ColorKey { get; private init; } = "Blue";

	public LucideIconKind Chevron => IsOpen ? LucideIconKind.ChevronDown : LucideIconKind.ChevronRight;
	public string Count { get; private init; } = "";
	public Avalonia.Thickness Indent => new(Depth * 20, 0, 0, 0);
	public bool ShowIcon => true;

	private static SearchRow Group(string key, string name, int depth, bool open, int count, string? parent = null) {
		var (icon, color) = depth == 0 ? PageLook(name) : CategoryLook(parent ?? "", name);
		return new SearchRow { Key = key, Text = name, Count = count.ToString(), IsGroup = true, IsOpen = open, Depth = depth, Icon = icon, ColorKey = color };
	}

	private static (LucideIconKind Icon, string Color) CategoryLook(string group, string category) {
		switch (group) {
			case "Assets": {
				var asset = editor.Assets.Types.AssetTypeRegistry.ByCppTypeName(category) ?? editor.Assets.Types.AssetTypeRegistry.ByType(category.Replace("Asset", "").ToLowerInvariant());
				return asset is null ? (LucideIconKind.Image, "Orange") : (asset.Icon, asset.ChipColor);
			}
			case "Nodes": return (LucideIconKind.Box, "Blue");
			case "Events": return (LucideIconKind.CirclePlay, "Red");
			case "Libraries": return (LucideIconKind.Package, "Cyan");
			default: return (LucideIconKind.Boxes, "Magenta");
		}
	}

	private static SearchRow Of(NodeEntry entry, int depth) {
		var (icon, color) = Look(entry);
		return new SearchRow { Text = entry.Title, Entry = entry, Depth = depth, Icon = icon, ColorKey = color };
	}

	public static (LucideIconKind Icon, string Color) Look(NodeEntry entry) => entry.Kind switch {
		NodeKind.GetVariable or NodeKind.GetGlobal or NodeKind.Member or NodeKind.GetSelf => (LucideIconKind.Download, "Blue"),
		NodeKind.SetVariable => (LucideIconKind.Pencil, "Orange"),
		NodeKind.CallMethod => (LucideIconKind.Package, "Cyan"),
		NodeKind.CallGlobal => (LucideIconKind.Braces, "Magenta"),
		NodeKind.Lambda => (LucideIconKind.SquareFunction, "Orange"),
		NodeKind.If or NodeKind.While or NodeKind.Repeat or NodeKind.NumericFor or NodeKind.GenericFor or NodeKind.Do or NodeKind.Return or NodeKind.Break or NodeKind.Sequence => (LucideIconKind.GitBranch, "Red"),
		NodeKind.Local or NodeKind.Cast => (LucideIconKind.Variable, "Green"),
		NodeKind.Comment => (LucideIconKind.MessageSquare, "Yellow"),
		NodeKind.RawStatement or NodeKind.RawExpression => (LucideIconKind.CodeXml, "Yellow"),
		_ => (LucideIconKind.CircleDot, "Green")
	};

	public static string GroupOf(NodeEntry e) {
		if (e.Category == "Tables") return "Tables";
		switch (e.Kind) {
			case NodeKind.GetSelf or NodeKind.GetVariable or NodeKind.SetVariable:
				return "This script";
			case NodeKind.If or NodeKind.While or NodeKind.Repeat or NodeKind.NumericFor or NodeKind.GenericFor or NodeKind.Do or NodeKind.Return or NodeKind.Break or NodeKind.Sequence:
				return "Flow";
			case NodeKind.Binary or NodeKind.Unary or NodeKind.VectorConstructor:
				return "Math";
			case NodeKind.Lambda:
				return "Values";
			case NodeKind.Local or NodeKind.Cast or NodeKind.Number or NodeKind.String or NodeKind.Boolean or NodeKind.Nil:
				return "Values";
			case NodeKind.Comment or NodeKind.RawStatement or NodeKind.RawExpression:
				return "Comments and raw Lua";
		}
		if (e.Owner == GraphSession.ScriptOwner || e.Category.StartsWith("C++ ", StringComparison.Ordinal)) return "This script";
		if (e.Kind is NodeKind.CallGlobal or NodeKind.GetGlobal && e.Owner is null) return "Lua library";
		if (e.Owner is { Length: > 0 } lib && char.IsLower(lib[0])) return "Lua library";
		var owner = e.Owner ?? e.Category;
		if (owner.Contains("Asset", StringComparison.OrdinalIgnoreCase)) return "Engine assets";
		if (owner.Contains("Event", StringComparison.OrdinalIgnoreCase)) return "Engine events";
		return "Engine nodes";
	}

	public static string CategoryOf(NodeEntry e) {
		var group = GroupOf(e);
		switch (group) {
			case "This script":
				return e.Kind switch {
					NodeKind.GetSelf => "Self",
					NodeKind.GetVariable or NodeKind.SetVariable when e.Owner == GraphSession.BaseOwner => e.Category,
					NodeKind.GetVariable or NodeKind.SetVariable => "Variables",
					_ when e.Owner == GraphSession.ScriptOwner => "Functions",
					_ => e.Category
				};
			case "Math":
				return e.Kind == NodeKind.VectorConstructor ? "Vectors" : e.Binary is BinaryKind.Eq or BinaryKind.Ne or BinaryKind.Lt or BinaryKind.Le or BinaryKind.Gt or BinaryKind.Ge ? "Compare"
					: e.Binary is BinaryKind.And or BinaryKind.Or || e.Unary == UnaryKind.Not ? "Logic" : "Arithmetic";
			case "Lua library":
				return e.Owner ?? (e.Kind == NodeKind.GetGlobal ? "Globals" : "Basic");
			default:
				return group == e.Category ? group : e.Owner ?? e.Category;
		}
	}

	private static (LucideIconKind Icon, string Color) PageLook(string name) => name switch {
		"Best matches" => (LucideIconKind.Search, "Yellow"),
		"This script" => (LucideIconKind.CodeXml, "Magenta"),
		"Flow" => (LucideIconKind.GitBranch, "Red"),
		"Tables" => (LucideIconKind.Braces, "Cyan"),
		"Math" => (LucideIconKind.Plus, "Orange"),
		"Values" => (LucideIconKind.CircleDot, "Green"),
		"Lua library" => (LucideIconKind.Package, "Cyan"),
		"Engine assets" => (LucideIconKind.Image, "Orange"),
		"Engine events" => (LucideIconKind.CirclePlay, "Red"),
		"Comments and raw Lua" => (LucideIconKind.MessageSquare, "Yellow"),
		_ => (LucideIconKind.Boxes, "Blue")
	};

	private static readonly string[] s_order = ["Best matches", "This script", "Flow", "Tables", "Math", "Values", "Lua library", "Engine nodes", "Engine assets", "Engine events", "Comments and raw Lua"];

	public static List<SearchRow> Tree(IReadOnlyList<NodeEntry> entries, ISet<string> open, bool everythingOpen) {
		var rows = new List<SearchRow>();
		if (everythingOpen && entries.Count > 12) {
			rows.Add(Group("Best matches", "Best matches", 0, true, Math.Min(10, entries.Count)));
			rows.AddRange(entries.Take(10).Select(e => Of(e, 1)));
		}
		var groups = entries.GroupBy(GroupOf).OrderBy(g => Array.IndexOf(s_order, g.Key) is var at and >= 0 ? at : s_order.Length);
		foreach (var group in groups) {
			var groupOpen = everythingOpen || open.Contains(group.Key);
			rows.Add(Group(group.Key, group.Key, 0, groupOpen, group.Count()));
			if (!groupOpen) continue;
			var categories = group.GroupBy(CategoryOf).ToList();
			var flat = categories.Count == 1 || group.Count() <= 10;
			foreach (var category in categories) {
				if (flat) {
					rows.AddRange(category.Select(e => Of(e, 1)));
					continue;
				}
				var key = group.Key + "/" + category.Key;
				var categoryOpen = everythingOpen || open.Contains(key);
				rows.Add(Group(key, category.Key, 1, categoryOpen, category.Count(), group.Key));
				if (categoryOpen) rows.AddRange(category.Select(e => Of(e, 2)));
			}
		}
		return rows;
	}
}
