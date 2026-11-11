using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Editing;

public enum NodeKind {
	If, While, Repeat, NumericFor, GenericFor, Do, Return, Break, Sequence, Local, Cast, Comment, RawStatement,
	Binary, Unary, Number, String, Boolean, Nil, MakeTable, RawExpression, Index, Paren,
	CallGlobal, CallMethod, Member, GetGlobal, VectorConstructor, GetVariable, SetVariable, GetSelf, SetIndex, SetMember, Lambda
}

public sealed record NodeEntry(NodeKind Kind, string Title, string Category, string Search, string? Name = null, string? Owner = null,
	BinaryKind? Binary = null, UnaryKind? Unary = null, LuaType? Result = null, LuaType? FirstInput = null, string Doc = "", int Arity = 0, Binding? Binding = null, FunctionType? Signature = null, bool DotCall = false) {
	public bool IsStatement => Kind is NodeKind.If or NodeKind.While or NodeKind.Repeat or NodeKind.NumericFor or NodeKind.GenericFor or NodeKind.Do
		or NodeKind.Return or NodeKind.Break or NodeKind.Sequence or NodeKind.Local or NodeKind.Cast or NodeKind.Comment or NodeKind.RawStatement or NodeKind.SetVariable or NodeKind.SetIndex or NodeKind.SetMember;
}

public sealed class NodeCatalog {
	private static readonly List<NodeEntry> s_builtins = MakeBuiltins();

	private readonly List<NodeEntry> m_entries = [];
	private readonly TypeUniverse m_universe;
	private readonly TypeChecker m_checker;

	public NodeCatalog(TypeUniverse universe) {
		m_universe = universe;
		m_checker = new TypeChecker(universe);
		m_entries.AddRange(s_builtins);
		AddGlobals();
		AddClasses();
	}

	public IReadOnlyList<NodeEntry> Entries => m_entries;

	public static NodeEntry Of(NodeKind kind) => s_builtins.First(e => e.Kind == kind);

	public static NodeEntry Operator(BinaryKind op) => s_builtins.First(e => e.Binary == op);

	public static NodeEntry Operator(UnaryKind op) => s_builtins.First(e => e.Unary == op);

	public static NodeEntry Vector(string name) => s_builtins.First(e => e.Kind == NodeKind.VectorConstructor && e.Name == name);

	private static List<NodeEntry> MakeBuiltins() {
		var list = new List<NodeEntry> {
			new(NodeKind.If, "if", "Flow", "if branch else condition"),
			new(NodeKind.Sequence, "Sequence", "Flow", "sequence then run in order"),
			new(NodeKind.NumericFor, "for i =", "Flow", "for loop range index count"),
			new(NodeKind.GenericFor, "for in", "Flow", "foreach for each pairs ipairs loop"),
			new(NodeKind.While, "while", "Flow", "while loop"),
			new(NodeKind.Repeat, "repeat until", "Flow", "repeat until loop"),
			new(NodeKind.Do, "do", "Flow", "do block scope"),
			new(NodeKind.Return, "return", "Flow", "return exit"),
			new(NodeKind.Break, "break", "Flow", "break leave loop"),
			new(NodeKind.Local, "local", "Variables", "local variable declare let"),
			new(NodeKind.Cast, "---@cast", "Variables", "cast type convert"),
			new(NodeKind.Comment, "-- comment", "Misc", "comment note"),
			new(NodeKind.RawStatement, "raw Lua", "Misc", "raw lua code text"),
			new(NodeKind.RawExpression, "raw Lua value", "Misc", "raw lua expression code text"),
			new(NodeKind.Lambda, "Function  function(...) end", "Functions", "function lambda closure callback anonymous fn handler", Result: new PrimitiveType(PrimitiveKind.Function)),
			new(NodeKind.MakeTable, "Table", "Tables", "table record struct fields named {} make", Name: "record", Result: LuaType.Table),
			new(NodeKind.MakeTable, "Array", "Tables", "array list items positional make {1, 2, 3}", Name: "array", Result: new ArrayType(LuaType.Any)),
			new(NodeKind.MakeTable, "Dictionary", "Tables", "dictionary map keys values make {[k] = v}", Name: "dictionary", Result: new TableType(LuaType.Any, LuaType.Any)),
			new(NodeKind.Index, "Get item  t[key]", "Tables", "get item element index array dictionary key []"),
			new(NodeKind.SetIndex, "Set item  t[key] = value", "Tables", "set item element index array dictionary key [] ="),
			new(NodeKind.Member, "Get field  t.name", "Tables", "get field member table record dot", Name: "name"),
			new(NodeKind.SetMember, "Set field  t.name = value", "Tables", "set field member table record dot =", Name: "name"),
			new(NodeKind.CallGlobal, "Add to array  table.insert", "Tables", "add append push insert array list item", Name: "table.insert", Arity: 2),
			new(NodeKind.CallGlobal, "Remove from array  table.remove", "Tables", "remove pop delete array list item", Name: "table.remove", Arity: 2),
			new(NodeKind.GenericFor, "For each item  ipairs", "Tables", "for each item array list ipairs loop", Name: "ipairs"),
			new(NodeKind.GenericFor, "For each pair  pairs", "Tables", "for each pair dictionary table pairs key value loop", Name: "pairs"),

			new(NodeKind.Number, "number", "Values", "number constant float integer"),
			new(NodeKind.String, "string", "Values", "string text constant"),
			new(NodeKind.Boolean, "boolean", "Values", "boolean true false bool"),
			new(NodeKind.Nil, "nil", "Values", "nil nothing null"),
new(NodeKind.VectorConstructor, "vec2", "Values", "vec2 vector 2d", Name: "vec2", Arity: 2, Result: new NamedType("vec2", [])),
			new(NodeKind.VectorConstructor, "vec3", "Values", "vec3 vector 3d position", Name: "vec3", Arity: 3, Result: new NamedType("vec3", [])),
			new(NodeKind.VectorConstructor, "quat", "Values", "quat quaternion rotation", Name: "quat", Arity: 4, Result: new NamedType("quat", []))
		};
		var arithmetic = new (BinaryKind Op, string Title, string Words)[] {
			(BinaryKind.Add, "+", "add plus sum +"), (BinaryKind.Sub, "-", "subtract minus -"),
			(BinaryKind.Mul, "*", "multiply times *"), (BinaryKind.Div, "/", "divide /"),
			(BinaryKind.FloorDiv, "//", "floor divide //"), (BinaryKind.Mod, "%", "modulo remainder %"),
			(BinaryKind.Pow, "^", "power exponent ^"), (BinaryKind.Concat, "..", "concat join text .."),
			(BinaryKind.Eq, "==", "equal == compare"), (BinaryKind.Ne, "~=", "not equal ~= compare"),
			(BinaryKind.Lt, "<", "less < compare"), (BinaryKind.Le, "<=", "less equal <= compare"),
			(BinaryKind.Gt, ">", "greater > compare"), (BinaryKind.Ge, ">=", "greater equal >= compare"),
			(BinaryKind.And, "and", "and boolean"), (BinaryKind.Or, "or", "or boolean default"),
			(BinaryKind.BitAnd, "&", "bit and &"), (BinaryKind.BitOr, "|", "bit or |"),
			(BinaryKind.BitXor, "~", "bit xor ~"), (BinaryKind.Shl, "<<", "shift left <<"), (BinaryKind.Shr, ">>", "shift right >>")
		};
		foreach (var (op, title, words) in arithmetic) list.Add(new NodeEntry(NodeKind.Binary, title, "Operators", words, Binary: op));
		list.Add(new NodeEntry(NodeKind.Unary, "-", "Operators", "negate minus -", Unary: UnaryKind.Neg));
		list.Add(new NodeEntry(NodeKind.Unary, "not", "Operators", "not ! invert", Unary: UnaryKind.Not));
		list.Add(new NodeEntry(NodeKind.Unary, "#", "Operators", "length count size #", Unary: UnaryKind.Len));
		list.Add(new NodeEntry(NodeKind.Unary, "~", "Operators", "bit not ~", Unary: UnaryKind.BitNot));
		return list;
	}

	private void AddGlobals() {
		foreach (var global in m_universe.Globals.OrderBy(g => g.Name, StringComparer.Ordinal)) {
			if (global.Name is "vec2" or "vec3" or "quat") continue;
			if (global.Type is FunctionType function) {
				m_entries.Add(Function(global.Name, global.Name, "Functions", function, global.Doc, null));
				continue;
			}

			m_entries.Add(new NodeEntry(NodeKind.GetGlobal, global.Name, "Globals", global.Name + " " + global.Doc, Name: global.Name, Result: global.Type, Doc: global.Doc));
			if (global.Type is not NamedType named || m_universe.FindClass(named.Name) is null) continue;
			foreach (var member in m_universe.MembersOf(named.Name)) {
				var path = global.Name + (member.Decl.IsMethod ? ":" : ".") + member.Decl.Name;
				if (member.Decl.Type is FunctionType f) m_entries.Add(Function(path, global.Name + "." + member.Decl.Name, global.Name, f, member.Decl.Doc, global.Name));
				else m_entries.Add(new NodeEntry(NodeKind.Member, global.Name + "." + member.Decl.Name, global.Name, global.Name + "." + member.Decl.Name, Name: member.Decl.Name, Owner: global.Name, Result: member.Decl.Type, Doc: member.Decl.Doc));
			}
		}
	}

	private static NodeEntry Function(string title, string path, string category, FunctionType function, string doc, string? owner) {
		var first = function.Params.Count > 0 ? function.Params[0].Type : null;
		var result = function.Returns.Count > 0 ? function.Returns[0] : null;
		return new NodeEntry(NodeKind.CallGlobal, title, category, path + " " + doc, Name: path, Owner: owner, Result: result, FirstInput: first, Doc: doc);
	}

	private void AddClasses() {
		foreach (var cls in m_universe.Classes.OrderBy(c => c.Name, StringComparer.Ordinal)) {
			foreach (var member in m_universe.MembersOf(cls.Name).Where(m => m.DeclaringClass == cls.Name)) {
				var decl = member.Decl;
				if (decl.Type is FunctionType f && decl.IsMethod) {
					m_entries.Add(new NodeEntry(NodeKind.CallMethod, cls.Name + ":" + decl.Name, cls.Name, cls.Name + " " + decl.Name + " " + decl.Doc, Name: decl.Name, Owner: cls.Name,
						Result: f.Returns.Count > 0 ? f.Returns[0] : null, FirstInput: new NamedType(cls.Name, []), Doc: decl.Doc));
				} else if (decl.Type is not FunctionType) {
					m_entries.Add(new NodeEntry(NodeKind.Member, cls.Name + "." + decl.Name, cls.Name, cls.Name + " " + decl.Name + " " + decl.Doc, Name: decl.Name, Owner: cls.Name,
						Result: decl.Type, FirstInput: new NamedType(cls.Name, []), Doc: decl.Doc));
				}
			}
		}
	}

	public IReadOnlyList<NodeEntry> Search(string text, LuaType? producing = null, LuaType? expecting = null, bool statementsOnly = false) {
		var words = text.Split(' ', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
		var scored = new List<(NodeEntry Entry, int Score)>();
		foreach (var entry in m_entries) {
			if (statementsOnly && !entry.IsStatement && entry.Kind is not (NodeKind.CallGlobal or NodeKind.CallMethod)) continue;
			if (producing is not null && !AcceptsAsFirstInput(entry, producing)) continue;
			if (expecting is not null && !Produces(entry, expecting)) continue;
			var score = Score(entry, words);
			if (score >= 0) scored.Add((entry, score));
		}
		return scored.OrderBy(s => s.Score).ThenBy(s => s.Entry.Category, StringComparer.Ordinal).ThenBy(s => s.Entry.Title, StringComparer.Ordinal).Select(s => s.Entry).ToList();
	}

	private static int Score(NodeEntry entry, string[] words) {
		if (words.Length == 0) return 5;
		var total = 0;
		foreach (var word in words) {
			if (entry.Title.StartsWith(word, StringComparison.OrdinalIgnoreCase)) total += 0;
			else if (entry.Title.Contains(word, StringComparison.OrdinalIgnoreCase)) total += 1;
			else if (entry.Search.Contains(word, StringComparison.OrdinalIgnoreCase)) total += 3;
			else return -1;
		}
		return total;
	}

	private bool AcceptsAsFirstInput(NodeEntry entry, LuaType produced) {
		switch (entry.Kind) {
			case NodeKind.Binary or NodeKind.Unary or NodeKind.Index or NodeKind.MakeTable:
			case NodeKind.Return or NodeKind.Local or NodeKind.Cast or NodeKind.If or NodeKind.While or NodeKind.Repeat or NodeKind.GenericFor or NodeKind.NumericFor:
				return true;
			case NodeKind.CallGlobal or NodeKind.CallMethod or NodeKind.Member:
				return entry.FirstInput is null ? entry.Kind == NodeKind.CallGlobal : m_checker.Check(produced, entry.FirstInput).Kind != Compat.Blocked;
			default:
				return false;
		}
	}

	private static LuaType Strip(LuaType type) => type is UnionType u && u.Members.Count(m => m != LuaType.Nil) == 1 ? u.Members.First(m => m != LuaType.Nil) : type;

	private bool Produces(NodeEntry entry, LuaType expected) {
		if (entry.IsStatement) return false;
		if (entry.Kind == NodeKind.Lambda) return Strip(expected) is FunctionType or PrimitiveType { Kind: PrimitiveKind.Function or PrimitiveKind.Any or PrimitiveKind.Unknown };
		if (entry.Result is null) return entry.Kind is NodeKind.Binary or NodeKind.Unary or NodeKind.RawExpression or NodeKind.Index or NodeKind.Paren;
		return m_checker.Check(entry.Result, expected).Kind != Compat.Blocked;
	}
}
