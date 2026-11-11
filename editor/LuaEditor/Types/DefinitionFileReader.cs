using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;

namespace editor.LuaEditor.Types;

public static partial class DefinitionFileReader {
	[GeneratedRegex(@"^(local\s+)?function\s+([A-Za-z_][\w.]*)([:.])([A-Za-z_]\w*)\s*\(([^)]*)\)")]
	private static partial Regex MemberFunction();

	[GeneratedRegex(@"^(local\s+)?function\s+([A-Za-z_]\w*)\s*\(([^)]*)\)")]
	private static partial Regex GlobalFunction();

	[GeneratedRegex(@"^(local\s+)?([A-Za-z_]\w*)(\.([A-Za-z_]\w*))?\s*=(?!=)")]
	private static partial Regex Assignment();

	[GeneratedRegex(@"^\s*\[?""?([A-Za-z_]\w*)""?\]?\s*=\s*([^,]+?)\s*,?\s*$")]
	private static partial Regex EnumEntry();

	public static DeclarationFile Read(string path, string text) => new Reader(path, text).Run();

	private sealed class ClassBuilder(string name, SourceLocation location) {
		public readonly string Name = name;
		public readonly SourceLocation Location = location;
		public readonly List<GenericParam> Generics = [];
		public readonly List<LuaType> Bases = [];
		public readonly List<MemberDecl> Members = [];
		public readonly List<OperatorDecl> Operators = [];
		public string Doc = "";

		public ClassDecl Build() => new(Name, Generics, Bases, Members, Doc, Location, Operators);
	}

	private sealed class Reader(string path, string text) {
		private readonly string[] m_lines = text.Replace("\r\n", "\n").Split('\n');
		private readonly List<ClassBuilder> m_classes = [];
		private readonly Dictionary<string, ClassBuilder> m_classByName = [];
		private readonly Dictionary<string, string> m_localToClass = [];
		private readonly List<GlobalDecl> m_globals = [];
		private readonly List<AliasDecl> m_aliases = [];
		private readonly List<EnumDecl> m_enums = [];
		private readonly List<Diagnostic> m_diagnostics = [];

		private static readonly AnnotationBlock s_empty = new("", [], []);

		private SourceLocation At(int index) => new(path, index + 1);

		public DeclarationFile Run() {
			var i = 0;
			while (i < m_lines.Length) {
				var line = m_lines[i].Trim();
				if (line.StartsWith("---")) {
					var start = i;
					var blockLines = new List<string>();
					while (i < m_lines.Length && m_lines[i].TrimStart().StartsWith("---")) blockLines.Add(m_lines[i++]);
					var block = AnnotationParser.Parse(blockLines);
					foreach (var error in block.Errors) m_diagnostics.Add(new Diagnostic(DiagnosticSeverity.Warning, error, At(start)));

					var subject = i;
					while (subject < m_lines.Length && m_lines[subject].Trim().Length == 0) subject++;
					if (subject < m_lines.Length && !m_lines[subject].TrimStart().StartsWith("--")) {
						i = Process(block, start, subject);
					} else {
						Process(block, start, -1);
					}
				} else if (line.Length == 0 || line.StartsWith("--")) {
					i++;
				} else {
					i = Process(s_empty, i, i);
				}
			}

			return new DeclarationFile(path, m_classes.Select(c => c.Build()).ToList(), m_globals, m_aliases, m_enums, m_diagnostics);
		}

		private int Process(AnnotationBlock block, int blockStart, int subjectIndex) {
			var subject = subjectIndex >= 0 ? m_lines[subjectIndex].Trim() : "";
			var next = subjectIndex >= 0 ? subjectIndex + 1 : blockStart + 1;

			foreach (var alias in block.Tags.OfType<AliasTag>())
				m_aliases.Add(new AliasDecl(alias.Name, alias.Type, block.Doc, At(blockStart)));

			if (block.Tags.OfType<ClassTag>().FirstOrDefault() is { } classTag) {
				var builder = DeclareClass(classTag, block.Doc, At(blockStart));
				foreach (var op in block.Tags.OfType<OperatorTag>()) {
					var decl = new OperatorDecl(op.Op, op.Operand, op.Result);
					if (!builder.Operators.Contains(decl)) builder.Operators.Add(decl);
				}
				var line = blockStart;
				foreach (var field in block.Tags.OfType<FieldTag>()) {
					var type = field.Optional ? LuaType.Optional(field.Type) : field.Type;
					AddMember(builder, MakeMember(field.Name, type, field.Doc, null, false, [], [], At(line++)));
				}
				if (Assignment().Match(subject) is { Success: true } m && !m.Groups[3].Success) {
					// type markers must not overwrite their class definitions
					if (block.Tags.OfType<TypeTag>().FirstOrDefault() is { } marker) {
						if (!m.Groups[1].Success) AddGlobal(m.Groups[2].Value, marker.Type, block.Doc, At(subjectIndex));
					} else {
						m_localToClass[m.Groups[2].Value] = classTag.Name;
						if (!m.Groups[1].Success) AddGlobal(m.Groups[2].Value, new NamedType(classTag.Name, []), block.Doc, At(subjectIndex));
					}
				}
				return next;
			}

			if (block.Tags.OfType<EnumTag>().FirstOrDefault() is { } enumTag) {
				if (Assignment().Match(subject) is { Success: true } m && subject.Contains('{')) return ReadEnum(enumTag, m.Groups[2].Value, block.Doc, blockStart, subjectIndex);
				return next;
			}

			if (subjectIndex < 0) return next;

			if (MemberFunction().Match(subject) is { Success: true } mf) {
				if (mf.Groups[1].Success) return next;
				var owner = ResolveOwner(mf.Groups[2].Value, subjectIndex);
				var fn = BuildFunction(block, mf.Groups[5].Value);
				AddMember(owner, MakeMember(mf.Groups[4].Value, fn, block.Doc, Unit(block), mf.Groups[3].Value == ":",
					Generics(block), Overloads(block), At(subjectIndex)));
				return next;
			}

			if (GlobalFunction().Match(subject) is { Success: true } gf) {
				if (!gf.Groups[1].Success) AddGlobal(gf.Groups[2].Value, BuildFunction(block, gf.Groups[3].Value), block.Doc, At(subjectIndex), Generics(block), Overloads(block));
				return next;
			}

			if (block.Tags.OfType<TypeTag>().FirstOrDefault() is { } typeTag && Assignment().Match(subject) is { Success: true } assign) {
				if (assign.Groups[3].Success) {
					var owner = ResolveOwner(assign.Groups[2].Value, subjectIndex);
					AddMember(owner, MakeMember(assign.Groups[4].Value, typeTag.Type, block.Doc, Unit(block), false, [], [], At(subjectIndex)));
				} else if (!assign.Groups[1].Success) {
					AddGlobal(assign.Groups[2].Value, typeTag.Type, block.Doc, At(subjectIndex));
				}
			}
			return next;
		}

		private ClassBuilder DeclareClass(ClassTag tag, string doc, SourceLocation location) {
			if (!m_classByName.TryGetValue(tag.Name, out var builder)) {
				builder = new ClassBuilder(tag.Name, location);
				m_classByName[tag.Name] = builder;
				m_classes.Add(builder);
			}
			foreach (var g in tag.Generics) if (builder.Generics.All(existing => existing.Name != g.Name)) builder.Generics.Add(g);
			foreach (var b in tag.Bases) if (!builder.Bases.Contains(b)) builder.Bases.Add(b);
			if (builder.Doc.Length == 0) builder.Doc = doc;
			return builder;
		}

		private ClassBuilder ResolveOwner(string owner, int line) {
			var name = m_localToClass.GetValueOrDefault(owner, owner);
			if (m_classByName.TryGetValue(name, out var builder)) return builder;

			builder = DeclareClass(new ClassTag(name, [], []), "", At(line));
			if (m_globals.All(g => g.Name != owner)) AddGlobal(owner, new NamedType(name, []), "", At(line));
			return builder;
		}

		private void AddGlobal(string name, LuaType type, string doc, SourceLocation location,
			EquatableArray<GenericParam> generics = default, EquatableArray<FunctionType> overloads = default) {
			m_globals.RemoveAll(g => g.Name == name);
			m_globals.Add(new GlobalDecl(name, type, doc, location, generics, overloads));
		}

		private static void AddMember(ClassBuilder owner, MemberDecl member) {
			owner.Members.RemoveAll(m => m.Name == member.Name);
			owner.Members.Add(member);
		}

		private static MemberDecl MakeMember(string name, LuaType type, string doc, string? unit, bool isMethod,
			EquatableArray<GenericParam> generics, EquatableArray<FunctionType> overloads, SourceLocation location) {
			if (type is FunctionType { Params.Count: > 0 } fn && fn.Params[0].Name == "self") {
				isMethod = true;
				type = fn with { Params = fn.Params.Skip(1).ToList() };
			}
			return new MemberDecl(name, type, doc, unit, isMethod, generics, overloads, location);
		}

		private static FunctionType BuildFunction(AnnotationBlock block, string paramList) {
			var tags = block.Tags.OfType<ParamTag>().ToList();
			var parameters = new List<Param>();
			LuaType? vararg = null;
			foreach (var raw in paramList.Split(',')) {
				var name = raw.Trim();
				if (name.Length == 0) continue;
				var tag = tags.FirstOrDefault(t => t.Name == name);
				if (name == "...") vararg = tag?.Type ?? LuaType.Any;
				else parameters.Add(new Param(name, tag?.Type ?? LuaType.Any, tag?.Optional ?? false));
			}

			var returns = new List<LuaType>();
			LuaType? varargReturn = null;
			foreach (var r in block.Tags.OfType<ReturnTag>()) {
				if (r.Vararg) varargReturn = r.Type;
				else returns.Add(r.Type);
			}
			return new FunctionType(parameters, returns, vararg, varargReturn);
		}

		private static string? Unit(AnnotationBlock block) => block.Tags.OfType<UnitTag>().FirstOrDefault()?.Unit;

		private static EquatableArray<GenericParam> Generics(AnnotationBlock block)
			=> block.Tags.OfType<GenericTag>().SelectMany(g => g.Params).DistinctBy(g => g.Name).ToList();

		private static EquatableArray<FunctionType> Overloads(AnnotationBlock block)
			=> block.Tags.OfType<OverloadTag>()
				.Select(o => o.Signature is { Params.Count: > 0 } fn && fn.Params[0].Name == "self"
					? fn with { Params = fn.Params.Skip(1).ToList() }
					: o.Signature)
				.ToList();

		private int ReadEnum(EnumTag tag, string tableName, string doc, int blockStart, int subjectIndex) {
			var members = new List<(string, object?)>();
			var i = subjectIndex;
			var line = m_lines[i];
			var body = line[(line.IndexOf('{') + 1)..];
			while (true) {
				var close = body.IndexOf('}');
				foreach (var part in (close >= 0 ? body[..close] : body).Split(',')) {
					if (EnumEntry().Match(part) is { Success: true } e) members.Add((e.Groups[1].Value, ParseValue(e.Groups[2].Value)));
				}
				if (close >= 0 || ++i >= m_lines.Length) break;
				body = m_lines[i];
			}
			m_enums.Add(new EnumDecl(tag.Name, tableName, members, doc, At(blockStart)));
			return i + 1;
		}

		private static object? ParseValue(string value) {
			value = value.Trim();
			if (long.TryParse(value, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var l)) return l;
			if (double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var d)) return d;
			if (value.Length >= 2 && value[0] is '"' or '\'' && value[^1] == value[0]) return value[1..^1];
			if (value is "true" or "false") return value == "true";
			return null;
		}
	}
}
