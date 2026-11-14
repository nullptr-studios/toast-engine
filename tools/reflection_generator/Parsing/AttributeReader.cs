using ReflectionGenerator.Model;
using TreeSitter;

namespace ReflectionGenerator.Parsing;

internal static class AttributeReader
{
	public static AttributeList Read(Node declaration)
	{
		var attributes = new List<CppAttribute>();

		foreach (var list in declaration.ChildrenOfKind("attribute_declaration")) {
			foreach (var attribute in list.ChildrenOfKind("attribute")) {
				var name = attribute.Field("name");
				if (name is null) continue;

				var args = name.NextNamedSibling is { } argumentList
					? argumentList.Children.Where(a => a.IsNamed).Select(a => Unquote(a.Str())).ToList()
					: [];
				attributes.Add(new CppAttribute(name.Str(), args));
			}
		}

		return new AttributeList(attributes);
	}

	// string literal nodes come with their quotes
	private static string Unquote(string argument)
	{
		var text = argument.Trim();
		var quoted = text.Length >= 2 && text[0] == text[^1] && text[0] is '"' or '\'';
		return quoted ? text[1..^1] : text;
	}
}
