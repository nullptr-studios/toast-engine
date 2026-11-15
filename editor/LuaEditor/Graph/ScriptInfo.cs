using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Graph;

internal sealed record ScriptField(string Name, AssignStatement Statement, AnnotationBlock Annotations, InputPin Value);

internal sealed class ScriptInfo {
	public const string UnnamedClass = "__script";

	private ScriptInfo(LocalDecl declaration, Binding table, ClassTag? classTag, AnnotationBlock classAnnotations) {
		Declaration = declaration;
		Table = table;
		ClassTag = classTag;
		ClassAnnotations = classAnnotations;
	}

	public LocalDecl Declaration { get; }
	public Binding Table { get; }
	public ClassTag? ClassTag { get; }
	public AnnotationBlock ClassAnnotations { get; }
	public List<ScriptField> Fields { get; } = [];
	public List<FunctionDecl> Methods { get; } = [];

	public string ClassName => ClassTag?.Name ?? UnnamedClass;

	public static AnnotationBlock AnnotationsOf(Statement statement)
		=> AnnotationParser.Parse(statement.LeadingLines.Where(l => l.StartsWith("---", System.StringComparison.Ordinal)).ToList());

	public bool IsTable(InputPin pin)
		=> pin.Source is { } source && (source.Binding == Table || (source.Owner is VariableGet get && get.Binding == Table));

	public static ScriptInfo? Find(LuaGraph graph) {
		var module = graph.Module;
		var last = module.Statements.LastOrDefault(s => s is not CommentStatement and not CastStatement);
		LocalDecl? declaration = null;
		Binding? table = null;
		if (last is ReturnStatement { Values.Count: 1 } ret && ret.Values[0].Source is { Binding: { Declarer: LocalDecl returned } binding }) {
			declaration = returned;
			table = binding;
		}
		if (declaration is null) {
			declaration = module.Statements.OfType<LocalDecl>().FirstOrDefault(d => d.Names.Count == 1 && AnnotationsOf(d).Tags.OfType<ClassTag>().Any());
			table = declaration?.Names[0];
		}
		if (declaration is null || table is null) return null;

		var annotations = AnnotationsOf(declaration);
		var info = new ScriptInfo(declaration, table, annotations.Tags.OfType<ClassTag>().FirstOrDefault(), annotations);
		foreach (var statement in module.Statements) {
			switch (statement) {
				case AssignStatement { Targets.Count: 1, Values.Count: >= 1 } assign
					when assign.Targets[0] is { Kind: TargetKind.Member, Object: { } owner } target && info.IsTable(owner):
					info.Fields.Add(new ScriptField(target.Name!, assign, AnnotationsOf(assign), assign.Values[0]));
					break;
				case FunctionDecl { IsLocal: false, Path.Count: 2 } function when info.IsTable(function.Root):
					info.Methods.Add(function);
					break;
			}
		}
		return info;
	}

	public static FunctionType SignatureOf(FunctionEntry entry, AnnotationBlock annotations) {
		var parameters = entry.Params.Select(p => {
			var tag = annotations.Tags.OfType<ParamTag>().FirstOrDefault(t => t.Name == p.Name);
			return new Param(p.Name, tag?.Type ?? LuaType.Any, tag?.Optional ?? false);
		}).ToList();
		LuaType? vararg = entry.IsVararg ? annotations.Tags.OfType<ParamTag>().FirstOrDefault(t => t.Name == "...")?.Type ?? LuaType.Any : null;
		var returnTags = annotations.Tags.OfType<ReturnTag>().ToList();
		var returns = returnTags.Where(r => !r.Vararg).Select(r => r.Type).ToList();
		// unannotated functions may return any type
		var varargReturn = returnTags.FirstOrDefault(r => r.Vararg)?.Type ?? (returnTags.Count == 0 ? LuaType.Any : null);
		return new FunctionType(parameters, returns, vararg, varargReturn);
	}

	public static MemberDecl MemberOf(FunctionDecl function, AnnotationBlock annotations, string name, bool isMethod)
		=> new(name, SignatureOf(function.Entry, annotations), annotations.Doc, annotations.Tags.OfType<UnitTag>().FirstOrDefault()?.Unit, isMethod,
			annotations.Tags.OfType<GenericTag>().SelectMany(g => g.Params).DistinctBy(g => g.Name).ToList(),
			annotations.Tags.OfType<OverloadTag>().Select(o => o.Signature).ToList(), new SourceLocation("", 0));

	public DeclarationFile ToDeclarations(IReadOnlyDictionary<string, LuaType> fieldTypes) {
		var location = new SourceLocation("", 0);
		var members = new List<MemberDecl>();
		foreach (var field in Fields) {
			var type = field.Annotations.Tags.OfType<TypeTag>().FirstOrDefault()?.Type ?? fieldTypes.GetValueOrDefault(field.Name, LuaType.Any);
			members.Add(new MemberDecl(field.Name, type, field.Annotations.Doc, field.Annotations.Tags.OfType<UnitTag>().FirstOrDefault()?.Unit, false, [], [], location));
		}
		foreach (var method in Methods) members.RemoveAll(m => m.Name == method.Name);
		foreach (var method in Methods) members.Add(MemberOf(method, AnnotationsOf(method), method.Name, method.IsMethod));
		var bases = ClassTag?.Bases ?? [];
		var declaration = new ClassDecl(ClassName, ClassTag?.Generics ?? [], bases, members, ClassAnnotations.Doc, location);
		return new DeclarationFile("script", [declaration], [], [], [], []);
	}
}
