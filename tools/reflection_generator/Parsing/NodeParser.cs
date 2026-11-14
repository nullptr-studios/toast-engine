using ReflectionGenerator.Model;
using ReflectionGenerator.Rules;
using TreeSitter;

namespace ReflectionGenerator.Parsing;

internal static class NodeParser
{
	public static IReadOnlyList<NodeType> Parse(string source, string sourceFile)
	{
		using var tree = Syntax.Parse(source);
		return tree.RootNode.DescendantsOfKind("class_specifier")
			.Select(cls => ReadClass(cls, sourceFile))
			.OfType<NodeType>()
			.ToList();
	}

	private static NodeType? ReadClass(Node cls, string sourceFile)
	{
		var name = cls.Field("name")?.Str();
		if (name is null) return null;

		var attributes = AttributeReader.Read(cls);
		if (!attributes.Has("ToastNode")) return null;

		return new NodeType(
			name,
			cls.EnclosingNamespace(),
			ReadParent(cls),
			attributes.Without("ToastNode"),
			ReadTickFunctions(cls),
			ReadMethods(cls),
			ReadFields(cls),
			ReadSignals(cls),
			sourceFile);
	}

	private static ParentRef? ReadParent(Node cls)
	{
		foreach (var clause in cls.ChildrenOfKind("base_class_clause")) {
			foreach (var inner in clause.Children) {
				if (inner.Type == "type_identifier") return new ParentRef(inner.Str(), null);
				if (inner.Type != "qualified_identifier") continue;

				var full = inner.Str();
				var cut = full.LastIndexOf("::", StringComparison.Ordinal);
				return cut < 0 ? new ParentRef(full, null) : new ParentRef(full[(cut + 2)..], full[..cut]);
			}
		}
		return null;
	}

	private static IReadOnlyList<string> ReadTickFunctions(Node cls)
	{
		var found = new List<string>();
		foreach (var identifier in cls.DescendantsOfKind("field_identifier")) {
			var name = identifier.Str();
			if (identifier.Parent?.Type == "function_declarator" && TickFunctions.IsTickFunction(name) && !found.Contains(name)) {
				found.Add(name);
			}
		}
		return found;
	}

	private static IReadOnlyList<Field> ReadFields(Node cls)
	{
		var fields = new List<Field>();
		foreach (var (declaration, identifier) in MemberDeclarations(cls)) {
			var attributes = AttributeReader.Read(declaration);
			if (!attributes.Has("Reflect")) continue;

			var typeName = TypeText(declaration);
			if (typeName.Contains("Signal<")) continue;

			fields.Add(new Field(
				identifier.Str(),
				typeName,
				FieldKinds.Infer(typeName),
				typeName.Contains("vector<"),
				attributes.Without("Reflect"),
				declaration.Field("default_value")?.Str().Trim()));
		}
		return fields;
	}

	private static IReadOnlyList<Signal> ReadSignals(Node cls)
	{
		var signals = new List<Signal>();
		foreach (var (declaration, identifier) in MemberDeclarations(cls)) {
			var typeName = TypeText(declaration);
			if (!typeName.Contains("Signal<")) continue;

			signals.Add(new Signal(identifier.Str(), typeName, TemplateArgs.Split(typeName), AttributeReader.Read(declaration)));
		}
		return signals;
	}

	// nested struct definitions own their fields so they are skipped
	private static IEnumerable<(Node Declaration, Node Identifier)> MemberDeclarations(Node cls)
	{
		foreach (var identifier in cls.DescendantsOfKind("field_identifier")) {
			var declaration = identifier.Parent;
			if (declaration?.Type != "field_declaration") continue;
			if (declaration.Field("type")?.Type == "struct_specifier") continue;
			yield return (declaration, identifier);
		}
	}

	private static string TypeText(Node declaration) => declaration.Field("type")?.Str().Trim() ?? "";

	private static IReadOnlyList<Method> ReadMethods(Node cls)
	{
		var methods = new List<Method>();
		foreach (var declarator in cls.DescendantsOfKind("function_declarator")) {
			var name = declarator.Field("declarator");
			if (name?.Type != "field_identifier") continue;

			var declaration = EnclosingDeclaration(declarator);
			if (declaration is null) continue;

			var attributes = AttributeReader.Read(declaration);
			if (!attributes.Has("Reflect")) continue;
			if (methods.Any(m => m.Name == name.Str())) continue;

			methods.Add(new Method(
				name.Str(),
				ReturnType(declaration, declarator),
				ParameterReader.Read(declarator),
				IsConst(declarator),
				attributes.Without("Reflect")));
		}
		return methods;
	}

	private static Node? EnclosingDeclaration(Node declarator)
	{
		for (var parent = declarator.Parent; parent is not null; parent = parent.Parent) {
			if (parent.Type is "field_declaration" or "function_definition" or "declaration") return parent;
		}
		return null;
	}

	private static string ReturnType(Node declaration, Node declarator)
	{
		var type = declaration.Field("type")?.Str().Trim() ?? "";

		if (type == "auto") {
			var trailing = declarator.ChildrenOfKind("trailing_return_type").FirstOrDefault()?.NamedChildren.FirstOrDefault();
			if (trailing is not null) return trailing.Str().Trim();
		}

		// pointers and references live on the declarator and not on the type
		var suffix = "";
		for (var current = declaration.Field("declarator"); current is not null && current.Id != declarator.Id; current = current.Field("declarator")) {
			if (current.Type == "pointer_declarator") suffix += "*";
			if (current.Type == "reference_declarator") suffix += "&";
		}
		return type + suffix;
	}

	private static bool IsConst(Node declarator) =>
		declarator.ChildrenOfKind("type_qualifier").Any(q => q.Str().Trim() == "const");
}
