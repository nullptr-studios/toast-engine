using System;
using System.Collections.Generic;
using System.Linq;

namespace editor.LuaEditor.Types;

public enum Compat { Allowed = 0, Warning = 1, Blocked = 2 }

public readonly record struct CompatResult(Compat Kind, string? Reason) {
	public static readonly CompatResult Ok = new(Compat.Allowed, null);

	public static CompatResult Worst(CompatResult a, CompatResult b) => b.Kind > a.Kind ? b : a;
	public static CompatResult Best(CompatResult a, CompatResult b) => b.Kind < a.Kind ? b : a;

	public static CompatResult Warn(string reason) => new(Compat.Warning, reason);
	public static CompatResult Block(string reason) => new(Compat.Blocked, reason);
}

public sealed class TypeChecker(TypeUniverse universe) {
	public CompatResult Check(LuaType source, LuaType target)
		=> CheckResolved(universe.ResolveAliases(source), universe.ResolveAliases(target));

	private CompatResult CheckResolved(LuaType s, LuaType t) {
		if (IsAny(s) || IsAny(t) || IsUnknownName(s) || IsUnknownName(t)) return CompatResult.Ok;

		if (s is UnionType su) {
			if (su.Members.Any(IsAny)) return CompatResult.Ok;
			if (su.Members.Contains(LuaType.Nil) && !AcceptsNil(t)) {
				var rest = LuaType.Union(su.Members.Where(m => m != LuaType.Nil).ToArray());
				var r = CheckResolved(rest, t);
				return r.Kind == Compat.Allowed ? CompatResult.Warn($"{s} may be nil but {t} cannot be") : r;
			}
			return su.Members.Select(m => CheckResolved(m, t)).Aggregate(CompatResult.Ok, CompatResult.Worst);
		}

		if (t is UnionType tu) {
			var results = tu.Members.Select(m => CheckResolved(s, m)).ToList();
			var best = results.Aggregate(CompatResult.Block(""), CompatResult.Best);
			if (best.Kind != Compat.Blocked) return best;
			var advice = results.FirstOrDefault(r => r.Reason?.Contains("---@cast", StringComparison.Ordinal) == true);
			return advice.Reason is not null ? advice : Cannot(s, t);
		}

		if (s is LiteralType ls) return CheckLiteral(ls, t);

		if (t is LiteralType lt) {
			return CheckResolved(s, PrimitiveOf(lt)).Kind == Compat.Allowed
				? CompatResult.Warn($"{s} is not checked against the allowed value {t}")
				: Cannot(s, t);
		}

		if (EnumOf(t) is { } targetEnum) {
			return EnumOf(s) is { } sameEnum && sameEnum.Name == targetEnum.Name ? CompatResult.Ok : Cannot(s, t);
		}
		if (EnumOf(s) is { } sourceEnum) return CheckResolved(EnumValueType(sourceEnum), t);

		return (s, t) switch {
			(PrimitiveType ps, PrimitiveType pt) => CheckPrimitives(ps, pt),
			(_, PrimitiveType { Kind: PrimitiveKind.Table }) when s is ArrayType or TableType or NamedType => CompatResult.Ok,
			(PrimitiveType { Kind: PrimitiveKind.Table }, NamedType or ArrayType or TableType) => CompatResult.Ok,
			(FunctionType, PrimitiveType { Kind: PrimitiveKind.Function }) => CompatResult.Ok,
			(NamedType ns, NamedType nt) => CheckClasses(ns, nt),
			(ArrayType sa, ArrayType ta) => CheckResolved(sa.Element, ta.Element),
			(TableType st, TableType tt) => CompatResult.Worst(CheckResolved(st.Key, tt.Key), CheckResolved(st.Value, tt.Value)),
			(ArrayType sa, TableType tt) => CompatResult.Worst(CheckResolved(LuaType.Integer, tt.Key), CheckResolved(sa.Element, tt.Value)),
			(FunctionType sf, FunctionType tf) => CheckFunctions(sf, tf),
			(PrimitiveType { Kind: PrimitiveKind.Function }, FunctionType) => CompatResult.Warn($"the signature of this function is unknown, expected {t}"),
			_ => Cannot(s, t),
		};
	}

	private bool IsUnknownName(LuaType type)
		=> type is NamedType n && universe.FindClass(n.Name) is null && universe.FindAlias(n.Name) is null && universe.FindEnum(n.Name) is null;

	private static bool IsAny(LuaType type) => type is PrimitiveType { Kind: PrimitiveKind.Any or PrimitiveKind.Unknown };

	private static bool AcceptsNil(LuaType type)
		=> type == LuaType.Nil || IsAny(type) || (type is UnionType u && u.Members.Any(m => m == LuaType.Nil || IsAny(m)));

	private static CompatResult Cannot(LuaType s, LuaType t) => CompatResult.Block($"{s} cannot connect to {t}");

	private static LuaType PrimitiveOf(LiteralType literal) => literal.Value switch {
		string => LuaType.String,
		long => LuaType.Integer,
		bool => LuaType.Boolean,
		_ => LuaType.Number,
	};

	private CompatResult CheckLiteral(LiteralType s, LuaType t) {
		if (t is LiteralType lt) return Equals(s.Value, lt.Value) ? CompatResult.Ok : Cannot(s, t);
		if (EnumOf(t) is { } e) return e.Members.Any(m => Equals(m.Value, s.Value)) ? CompatResult.Ok : Cannot(s, t);
		var r = CheckResolved(PrimitiveOf(s), t);
		return r.Kind == Compat.Allowed ? r : Cannot(s, t);
	}

	private EnumDecl? EnumOf(LuaType type)
		=> type is NamedType { Args.Count: 0 } n && universe.FindClass(n.Name) is null ? universe.FindEnum(n.Name) : null;

	private static LuaType EnumValueType(EnumDecl e) {
		var kinds = e.Members.Select(m => m.Value switch {
			string => LuaType.String,
			long => LuaType.Integer,
			bool => LuaType.Boolean,
			null => LuaType.Any,
			_ => LuaType.Number,
		}).Distinct().ToArray();
		return kinds.Length == 0 ? LuaType.Integer : LuaType.Union(kinds);
	}

	private static CompatResult CheckPrimitives(PrimitiveType s, PrimitiveType t) {
		if (s.Kind == t.Kind) return CompatResult.Ok;
		if (s.Kind == PrimitiveKind.Integer && t.Kind == PrimitiveKind.Number) return CompatResult.Ok;
		if (s.Kind == PrimitiveKind.Number && t.Kind == PrimitiveKind.Integer)
			return CompatResult.Block("number cannot connect to integer; round it first (math.floor)");
		return Cannot(s, t);
	}

	private CompatResult CheckClasses(NamedType s, NamedType t) {
		if (s.Name == t.Name) {
			if (t.Args.Count == 0 || s.Args.Count == 0) return CompatResult.Ok;
			var same = s.Args.Count == t.Args.Count
				&& s.Args.Zip(t.Args).All(p => universe.ResolveAliases(p.First) == universe.ResolveAliases(p.Second));
			return same ? CompatResult.Ok : CompatResult.Block($"{s} cannot connect to {t}: the type arguments differ");
		}
		if (universe.IsSubclassOf(s.Name, t.Name)) return CompatResult.Ok;
		if (universe.FindClass(s.Name) is not null && universe.IsSubclassOf(t.Name, s.Name))
			return CompatResult.Block($"{s} is not always a {t}; use a ---@cast node");
		return Cannot(s, t);
	}

	private CompatResult CheckFunctions(FunctionType s, FunctionType t) {
		var result = CompatResult.Ok;
		for (var i = 0; i < s.Params.Count; i++) {
			if (i < t.Params.Count) {
				result = CompatResult.Worst(result, CheckResolved(t.Params[i].Type, s.Params[i].Type));
			} else if (t.VarargParam is { } vararg) {
				result = CompatResult.Worst(result, CheckResolved(vararg, s.Params[i].Type));
			} else if (!s.Params[i].Optional) {
				return CompatResult.Block($"{s} needs more arguments than {t} passes");
			}
		}
		for (var i = 0; i < t.Returns.Count; i++) {
			if (i < s.Returns.Count) result = CompatResult.Worst(result, CheckResolved(s.Returns[i], t.Returns[i]));
			else if (s.VarargReturn is { } vret) result = CompatResult.Worst(result, CheckResolved(vret, t.Returns[i]));
			else return CompatResult.Block($"{s} returns fewer values than {t}");
		}
		return result;
	}

	public LuaType NarrowNonNil(LuaType type) {
		type = universe.ResolveAliases(type);
		return type is UnionType u ? LuaType.Union(u.Members.Where(m => m != LuaType.Nil).ToArray()) : type;
	}

	public LuaType NarrowToTypeName(LuaType type, string luaTypeName) {
		type = universe.ResolveAliases(type);
		if (IsAny(type)) return PrimitiveForName(luaTypeName);
		var members = type is UnionType u ? u.Members.ToArray() : [type];
		var kept = members.Where(m => IsAny(m) || TypeNamesOf(m).Contains(luaTypeName))
			.Select(m => IsAny(m) ? PrimitiveForName(luaTypeName) : m).ToArray();
		return kept.Length == 0 ? LuaType.Unknown : LuaType.Union(kept);
	}

	private static LuaType PrimitiveForName(string name) => name switch {
		"nil" => LuaType.Nil,
		"boolean" => LuaType.Boolean,
		"number" => LuaType.Number,
		"string" => LuaType.String,
		"table" => LuaType.Table,
		"function" => LuaType.Function,
		"userdata" => new PrimitiveType(PrimitiveKind.Userdata),
		"thread" => new PrimitiveType(PrimitiveKind.Thread),
		_ => LuaType.Unknown,
	};

	private IEnumerable<string> TypeNamesOf(LuaType type) => type switch {
		PrimitiveType { Kind: PrimitiveKind.Integer or PrimitiveKind.Number } => ["number"],
		PrimitiveType p => [p.Kind.ToString().ToLowerInvariant()],
		LiteralType { Value: string } => ["string"],
		LiteralType { Value: bool } => ["boolean"],
		LiteralType => ["number"],
		ArrayType or TableType => ["table"],
		FunctionType => ["function"],
		NamedType n when EnumOf(n) is { } e => TypeNamesOf(EnumValueType(e)),
		NamedType => ["table", "userdata"],
		_ => [],
	};
}
