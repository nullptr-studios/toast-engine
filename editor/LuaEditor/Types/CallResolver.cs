using System.Collections.Generic;
using System.Linq;

namespace editor.LuaEditor.Types;

public sealed record ResolvedCall(FunctionType Signature, IReadOnlyDictionary<string, LuaType> Bindings, CompatResult Compat,
	EquatableArray<CompatResult> ArgResults);

public sealed class CallResolver(TypeChecker checker, TypeUniverse universe) {
	private static readonly FunctionType s_empty = new([], [], null, null);

	public ResolvedCall Resolve(MemberDecl member, IReadOnlyList<LuaType?> args) {
		if (member.Type is not FunctionType main)
			return new ResolvedCall(s_empty, new Dictionary<string, LuaType>(), CompatResult.Block($"{member.Name} is not a function"), []);

		var generics = member.Generics.DistinctBy(g => g.Name).ToDictionary(g => g.Name, g => g.Constraint);
		ResolvedCall? best = null;
		foreach (var candidate in new[] { main }.Concat(member.Overloads)) {
			var call = Evaluate(candidate, generics, args);
			if (best is null || call.Compat.Kind < best.Compat.Kind) best = call;
		}
		return best!;
	}

	private ResolvedCall Evaluate(FunctionType fn, Dictionary<string, LuaType?> generics, IReadOnlyList<LuaType?> args) {
		var bindings = new Dictionary<string, LuaType>();
		var boundBy = new Dictionary<string, int>();
		var argErrors = new Dictionary<int, CompatResult>();

		for (var i = 0; i < args.Count; i++) {
			if (args[i] is not { } arg || ParamType(fn, i) is not { } param) continue;
			var before = bindings.Keys.ToHashSet();
			if (!TryBind(universe.ResolveAliases(param), universe.ResolveAliases(arg), generics, bindings))
				argErrors[i] = CompatResult.Block($"{arg} does not match the other arguments of this call");
			foreach (var name in bindings.Keys.Where(k => !before.Contains(k))) boundBy[name] = i;
		}

		foreach (var (name, constraint) in generics) {
			if (!bindings.TryGetValue(name, out var bound)) {
				bindings[name] = constraint ?? LuaType.Any;
			} else if (constraint is not null && checker.Check(bound, constraint).Kind != Compat.Allowed) {
				argErrors[boundBy[name]] = CompatResult.Block($"{bound} is not a {constraint}");
			}
		}

		var signature = new FunctionType(
			fn.Params.Select(p => p with { Type = Substitute(p.Type, generics, bindings, true) }).ToList(),
			fn.Returns.Select(r => Substitute(r, generics, bindings, false)).ToList(),
			fn.VarargParam is null ? null : Substitute(fn.VarargParam, generics, bindings, true),
			fn.VarargReturn is null ? null : Substitute(fn.VarargReturn, generics, bindings, false));

		var results = new List<CompatResult>();
		for (var i = 0; i < args.Count; i++) {
			if (argErrors.TryGetValue(i, out var error)) {
				results.Add(error);
			} else if (args[i] is not { } arg) {
				results.Add(CompatResult.Ok);
			} else if (i < signature.Params.Count) {
				var p = signature.Params[i];
				results.Add(checker.Check(arg, p.Optional ? LuaType.Optional(p.Type) : p.Type));
			} else if (signature.VarargParam is { } vararg) {
				results.Add(checker.Check(arg, vararg));
			} else {
				results.Add(CompatResult.Warn("too many arguments"));
			}
		}

		var compat = results.Aggregate(CompatResult.Ok, CompatResult.Worst);
		return new ResolvedCall(signature, bindings, compat, results);
	}

	private static LuaType? ParamType(FunctionType fn, int index)
		=> index < fn.Params.Count ? fn.Params[index].Type : fn.VarargParam;

	private bool TryBind(LuaType param, LuaType arg, Dictionary<string, LuaType?> generics, Dictionary<string, LuaType> bindings) {
		switch (param) {
			case NamedType { Args.Count: 0 } n when generics.ContainsKey(n.Name):
				return Bind(n.Name, arg, bindings);
			case CaptureType c:
				return arg is LiteralType { Value: string className } ? Bind(c.Name, new NamedType(className, []), bindings) : true;
			case NamedType n when arg is NamedType a && a.Name == n.Name && a.Args.Count == n.Args.Count:
				for (var i = 0; i < n.Args.Count; i++)
					if (!TryBind(n.Args[i], a.Args[i], generics, bindings)) return false;
				return true;
			case ArrayType p when arg is ArrayType a:
				return TryBind(p.Element, a.Element, generics, bindings);
			case UnionType u:
				foreach (var member in u.Members) {
					if (!Mentions(member, generics) || !ShapeMatches(member, arg, generics)) continue;
					return TryBind(member, arg, generics, bindings);
				}
				return true;
			default:
				return true;
		}
	}

	private bool Bind(string name, LuaType type, Dictionary<string, LuaType> bindings) {
		if (!bindings.TryGetValue(name, out var existing)) {
			bindings[name] = type;
			return true;
		}
		if (existing == type || checker.Check(type, existing).Kind != Compat.Blocked) return true;
		if (checker.Check(existing, type).Kind == Compat.Blocked) return false;
		bindings[name] = type;
		return true;
	}

	private static bool ShapeMatches(LuaType param, LuaType arg, Dictionary<string, LuaType?> generics) => param switch {
		NamedType { Args.Count: 0 } n when generics.ContainsKey(n.Name) => true,
		CaptureType => arg is LiteralType { Value: string },
		NamedType n => arg is NamedType a && a.Name == n.Name,
		ArrayType => arg is ArrayType,
		_ => false,
	};

	private static bool Mentions(LuaType type, Dictionary<string, LuaType?> generics) => type switch {
		NamedType n => (n.Args.Count == 0 && generics.ContainsKey(n.Name)) || n.Args.Any(a => Mentions(a, generics)),
		CaptureType => true,
		ArrayType a => Mentions(a.Element, generics),
		TableType t => Mentions(t.Key, generics) || Mentions(t.Value, generics),
		UnionType u => u.Members.Any(m => Mentions(m, generics)),
		FunctionType f => f.Params.Any(p => Mentions(p.Type, generics)) || f.Returns.Any(r => Mentions(r, generics)),
		_ => false,
	};

	private static LuaType Substitute(LuaType type, Dictionary<string, LuaType?> generics, Dictionary<string, LuaType> bindings, bool inParam) {
		switch (type) {
			case NamedType { Args.Count: 0 } n when generics.ContainsKey(n.Name):
				return bindings.GetValueOrDefault(n.Name, LuaType.Any);
			case CaptureType c:
				return inParam ? LuaType.String : bindings.GetValueOrDefault(c.Name, LuaType.Any);
			case NamedType n:
				return n.Args.Count == 0 ? n : n with { Args = n.Args.Select(a => Substitute(a, generics, bindings, inParam)).ToList() };
			case UnionType u:
				return LuaType.Union(u.Members.Select(m => Substitute(m, generics, bindings, inParam)).ToArray());
			case ArrayType a:
				return new ArrayType(Substitute(a.Element, generics, bindings, inParam));
			case TableType t:
				return new TableType(Substitute(t.Key, generics, bindings, inParam), Substitute(t.Value, generics, bindings, inParam));
			case FunctionType f:
				return new FunctionType(
					f.Params.Select(p => p with { Type = Substitute(p.Type, generics, bindings, inParam) }).ToList(),
					f.Returns.Select(r => Substitute(r, generics, bindings, inParam)).ToList(),
					f.VarargParam is null ? null : Substitute(f.VarargParam, generics, bindings, inParam),
					f.VarargReturn is null ? null : Substitute(f.VarargReturn, generics, bindings, inParam));
			default:
				return type;
		}
	}
}
