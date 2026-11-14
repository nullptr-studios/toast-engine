using ReflectionGenerator.Model;
using ReflectionGenerator.Rules;
using TreeSitter;

namespace ReflectionGenerator.Parsing;

internal static class EventParser
{
	public static IReadOnlyList<EventType> Parse(string source, string sourceFile)
	{
		using var tree = Syntax.Parse(source);
		var root = tree.RootNode;
		var enumNames = EnumNames(root);

		var events = new List<EventType>();
		foreach (var node in root.Descendants().Where(n => n.Type is "class_specifier" or "struct_specifier")) {
			var name = node.Field("name")?.Str();
			if (name is null || !InheritsEvent(node, name)) continue;

			var fields = ReadFields(node, enumNames);
			var constructorCompatible = ConstructorMatches(node, name, fields);
			var unsupported = fields.FirstOrDefault(f => f.LuaType is null);
			var skipReason = unsupported is null ? null : $"field '{unsupported.Name}' has unsupported type '{unsupported.TypeName}'";

			events.Add(new EventType(
				name,
				node.EnclosingNamespace(),
				fields,
				sourceFile,
				constructorCompatible,
				Supported: skipReason is null,
				Sendable: skipReason is null && constructorCompatible && fields.All(f => f.Readable),
				skipReason));
		}
		return events;
	}

	// matches Event<Name> and ns::Event<Name> but not Event<SomethingElse>
	private static bool InheritsEvent(Node node, string name)
	{
		var needle = $"Event<{name}>";
		string[] accessWords = ["public", "protected", "private", "virtual"];

		foreach (var clause in node.ChildrenOfKind("base_class_clause")) {
			var bases = clause.Str().TrimStart().TrimStart(':').Split(',');
			foreach (var baseClass in bases) {
				var words = baseClass.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries).Where(w => !accessWords.Contains(w));
				var compact = string.Concat(words);
				if (compact == needle || compact.EndsWith("::" + needle, StringComparison.Ordinal)) return true;
			}
		}
		return false;
	}

	private static List<string> EnumNames(Node root) => root.DescendantsOfKind("enum_specifier")
		.Where(e => e.Field("name") is { Type: "type_identifier" } && e.Field("body") is not null)
		.Select(e => e.Field("name")!.Str())
		.ToList();

	private static IReadOnlyList<EventField> ReadFields(Node node, IReadOnlyCollection<string> enumNames)
	{
		var body = node.Field("body");
		if (body is null) return [];

		var fields = new List<EventField>();
		var isPublic = node.Type == "struct_specifier";

		foreach (var member in body.NamedChildren) {
			if (member.Type == "access_specifier") {
				isPublic = member.Str().TrimStart().StartsWith("public", StringComparison.Ordinal);
				continue;
			}
			if (!isPublic || member.Type != "field_declaration") continue;

			var type = member.Field("type");
			if (type is null || type.Type is "struct_specifier" or "class_specifier" or "enum_specifier") continue;

			var typeName = type.Str().Trim();
			if (typeName.Contains("Event<")) continue;

			foreach (var identifier in FieldIdentifiers(member)) {
				var declared = typeName + IndirectionSuffix(identifier, member);
				var lua = LuaTypes.ForEventField(declared, enumNames);
				fields.Add(new EventField(identifier.Str(), declared, lua?.Lua, lua?.Readable ?? false));
			}
		}
		return fields;
	}

	// int *a, &b declares different types from one type node
	private static string IndirectionSuffix(Node identifier, Node declaration)
	{
		var suffix = "";
		for (var parent = identifier.Parent; parent is not null && parent.Id != declaration.Id; parent = parent.Parent) {
			if (parent.Type == "pointer_declarator") suffix += "*";
			if (parent.Type == "reference_declarator") suffix += "&";
		}
		return suffix;
	}

	private static IEnumerable<Node> FieldIdentifiers(Node node)
	{
		if (node.Type == "field_identifier") {
			yield return node;
			yield break;
		}
		foreach (var child in node.NamedChildren) {
			foreach (var identifier in FieldIdentifiers(child)) {
				yield return identifier;
			}
		}
	}

	// the lua side builds the event by calling this constructor with every field in order
	private static bool ConstructorMatches(Node node, string name, IReadOnlyList<EventField> fields)
	{
		if (fields.Count == 0) return true;

		var hasConstructor = false;
		foreach (var declarator in node.DescendantsOfKind("function_declarator")) {
			if (declarator.Field("declarator")?.Str().Trim() != name) continue;

			hasConstructor = true;
			var parameters = ParameterReader.Read(declarator);
			if (parameters.Count == 0) return true;
			if (parameters.Count == fields.Count && parameters.Zip(fields).All(p => Normalized(p.First.Type) == Normalized(p.Second.TypeName))) {
				return true;
			}
		}
		return !hasConstructor;
	}

	private static string Normalized(string type) =>
		type.Replace("const", "").Replace("&", "").WithoutWhitespace();
}
