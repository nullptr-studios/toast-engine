using System.Collections.Generic;
using System.Linq;

namespace editor.LuaEditor.Types;

public sealed record ClassInfo(string Name, EquatableArray<GenericParam> Generics, EquatableArray<LuaType> Bases,
	EquatableArray<MemberDecl> Members, string Doc, EquatableArray<OperatorDecl> Operators = default);

public sealed record MemberInfo(MemberDecl Decl, string DeclaringClass);

public sealed class TypeUniverse {
	private readonly Dictionary<string, ClassInfo> m_classes;
	private readonly Dictionary<string, AliasDecl> m_aliases;
	private readonly Dictionary<string, EnumDecl> m_enums;
	private readonly Dictionary<string, GlobalDecl> m_globals;
	private readonly Dictionary<string, IReadOnlyList<MemberInfo>> m_memberCache = [];
	private readonly List<DeclarationFile> m_files;

	private TypeUniverse(List<DeclarationFile> files, Dictionary<string, ClassInfo> classes, Dictionary<string, AliasDecl> aliases,
		Dictionary<string, EnumDecl> enums, Dictionary<string, GlobalDecl> globals, List<Diagnostic> diagnostics) {
		m_files = files;
		m_classes = classes;
		m_aliases = aliases;
		m_enums = enums;
		m_globals = globals;
		Diagnostics = diagnostics;
	}

	public IReadOnlyList<Diagnostic> Diagnostics { get; }
	public IEnumerable<ClassInfo> Classes => m_classes.Values;
	public IEnumerable<GlobalDecl> Globals => m_globals.Values;

	public ClassInfo? FindClass(string name) => m_classes.GetValueOrDefault(name);
	public AliasDecl? FindAlias(string name) => m_aliases.GetValueOrDefault(name);
	public EnumDecl? FindEnum(string name) => m_enums.GetValueOrDefault(name);
	public LuaType? GlobalType(string name) => m_globals.GetValueOrDefault(name)?.Type;

	public GlobalDecl? GlobalDeclOf(string name) => m_globals.GetValueOrDefault(name);

	public IEnumerable<EnumDecl> Enums => m_enums.Values;

	public TypeUniverse Extend(DeclarationFile extra) => Build(m_files.Append(extra));

	public static TypeUniverse Build(IEnumerable<DeclarationFile> input) {
		var files = input.ToList();
		var diagnostics = new List<Diagnostic>();
		var classes = new Dictionary<string, (ClassDecl First, List<GenericParam> Generics, List<LuaType> Bases, List<MemberDecl> Members, List<OperatorDecl> Operators)>();
		var order = new List<string>();
		var aliases = new Dictionary<string, AliasDecl>();
		var enums = new Dictionary<string, EnumDecl>();
		var globals = new Dictionary<string, GlobalDecl>();
		var redirects = new List<(string From, string Into)>();

		foreach (var file in files) {
			diagnostics.AddRange(file.Diagnostics);
			foreach (var decl in file.Classes) {
				if (!classes.TryGetValue(decl.Name, out var entry)) {
					entry = (decl, [], [], [], []);
					classes[decl.Name] = entry;
					order.Add(decl.Name);
				}
				foreach (var g in decl.Generics) if (entry.Generics.All(existing => existing.Name != g.Name)) entry.Generics.Add(g);
				foreach (var b in decl.Bases) if (!entry.Bases.Contains(b)) entry.Bases.Add(b);
				foreach (var op in decl.Operators) if (!entry.Operators.Contains(op)) entry.Operators.Add(op);
				foreach (var member in decl.Members) {
					var existing = entry.Members.FindIndex(m => m.Name == member.Name);
					if (existing < 0) {
						entry.Members.Add(member);
						continue;
					}
					diagnostics.Add(new Diagnostic(DiagnosticSeverity.Info,
						$"{decl.Name}.{member.Name} is declared again; the later declaration wins", member.Location));
					entry.Members[existing] = member;
				}
			}
			foreach (var alias in file.Aliases) aliases[alias.Name] = alias;
			foreach (var e in file.Enums) enums[e.Name] = e;
			foreach (var g in file.Globals) {
				if (globals.TryGetValue(g.Name, out var existing) && ImplicitRedirect(existing, g) is var (from, into, kept)) {
					redirects.Add((from, into));
					globals[g.Name] = kept;
				} else {
					globals[g.Name] = g;
				}
			}
		}

		// library globals may use a different name from their class
		foreach (var (from, into) in redirects) {
			if (from == into || !classes.TryGetValue(from, out var source) || !classes.TryGetValue(into, out var target)) continue;
			foreach (var member in source.Members)
				if (target.Members.All(m => m.Name != member.Name)) target.Members.Add(member);
			classes.Remove(from);
			order.Remove(from);
		}

		foreach (var name in order) {
			var entry = classes[name];
			entry.Bases.RemoveAll(b => {
				if (b is NamedType nt && classes.ContainsKey(nt.Name)) return false;
				if (b is NamedType unknown)
					diagnostics.Add(new Diagnostic(DiagnosticSeverity.Warning, $"{name} derives from unknown class {unknown.Name}", entry.First.Location));
				return true;
			});
		}

		var state = new Dictionary<string, int>();
		foreach (var name in order) Visit(name);

		void Visit(string name) {
			if (state.GetValueOrDefault(name) != 0) return;
			state[name] = 1;
			var entry = classes[name];
			foreach (var b in entry.Bases.ToList()) {
				var baseName = ((NamedType)b).Name;
				if (state.GetValueOrDefault(baseName) == 1) {
					diagnostics.Add(new Diagnostic(DiagnosticSeverity.Error, $"inheritance cycle: {name} derives from {baseName}", entry.First.Location));
					entry.Bases.Remove(b);
					continue;
				}
				Visit(baseName);
			}
			state[name] = 2;
		}

		var infos = order.ToDictionary(n => n, n => {
			var e = classes[n];
			return new ClassInfo(n, e.Generics, e.Bases, e.Members, e.First.Doc, e.Operators);
		});
		return new TypeUniverse(files, infos, aliases, enums, globals, diagnostics);
	}

	private static (string From, string Into, GlobalDecl Kept)? ImplicitRedirect(GlobalDecl existing, GlobalDecl incoming) {
		if (existing.Type is not NamedType { Args.Count: 0 } a || incoming.Type is not NamedType { Args.Count: 0 } b || a.Name == b.Name) return null;
		if (b.Name == incoming.Name) return (b.Name, a.Name, existing);
		if (a.Name == existing.Name) return (a.Name, b.Name, incoming);
		return null;
	}

	public bool IsSubclassOf(string derived, string baseName) {
		if (derived == baseName) return true;
		var seen = new HashSet<string>();
		var queue = new Queue<string>();
		queue.Enqueue(derived);
		while (queue.Count > 0) {
			var current = queue.Dequeue();
			if (!seen.Add(current) || !m_classes.TryGetValue(current, out var info)) continue;
			foreach (var b in info.Bases) {
				var name = ((NamedType)b).Name;
				if (name == baseName) return true;
				queue.Enqueue(name);
			}
		}
		return false;
	}

	public IReadOnlyList<MemberInfo> MembersOf(string className) {
		if (m_memberCache.TryGetValue(className, out var cached)) return cached;
		var result = new List<MemberInfo>();
		var names = new HashSet<string>();
		var seen = new HashSet<string>();
		var queue = new Queue<string>();
		queue.Enqueue(className);
		while (queue.Count > 0) {
			var current = queue.Dequeue();
			if (!seen.Add(current) || !m_classes.TryGetValue(current, out var info)) continue;
			foreach (var member in info.Members)
				if (names.Add(member.Name)) result.Add(new MemberInfo(member, current));
			foreach (var b in info.Bases) queue.Enqueue(((NamedType)b).Name);
		}
		m_memberCache[className] = result;
		return result;
	}

	public IReadOnlyList<OperatorDecl> OperatorsOf(string className) {
		var result = new List<OperatorDecl>();
		var seenClasses = new HashSet<string>();
		var queue = new Queue<string>();
		queue.Enqueue(className);
		while (queue.Count > 0) {
			var current = queue.Dequeue();
			if (!seenClasses.Add(current) || !m_classes.TryGetValue(current, out var info)) continue;
			foreach (var op in info.Operators)
				if (!result.Any(r => r.Op == op.Op && r.Operand == op.Operand)) result.Add(op);
			foreach (var b in info.Bases) queue.Enqueue(((NamedType)b).Name);
		}
		return result;
	}

	public LuaType ResolveAliases(LuaType type) => Resolve(type, []);

	private LuaType Resolve(LuaType type, HashSet<string> expanding) {
		switch (type) {
			case NamedType { Args.Count: 0 } n when m_aliases.TryGetValue(n.Name, out var alias):
				if (!expanding.Add(n.Name)) return n;
				var resolved = Resolve(alias.Type, expanding);
				expanding.Remove(n.Name);
				return resolved;
			case NamedType n:
				return n.Args.Count == 0 ? n : n with { Args = n.Args.Select(a => Resolve(a, expanding)).ToList() };
			case UnionType u:
				return LuaType.Union(u.Members.Select(m => Resolve(m, expanding)).ToArray());
			case ArrayType a:
				return new ArrayType(Resolve(a.Element, expanding));
			case TableType t:
				return new TableType(Resolve(t.Key, expanding), Resolve(t.Value, expanding));
			case FunctionType f:
				return new FunctionType(
					f.Params.Select(p => p with { Type = Resolve(p.Type, expanding) }).ToList(),
					f.Returns.Select(r => Resolve(r, expanding)).ToList(),
					f.VarargParam is null ? null : Resolve(f.VarargParam, expanding),
					f.VarargReturn is null ? null : Resolve(f.VarargReturn, expanding));
			default:
				return type;
		}
	}
}
