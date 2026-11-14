using ReflectionGenerator.Model;
using TreeSitter;

namespace ReflectionGenerator.Parsing;

internal static class ParameterReader
{
	public static IReadOnlyList<Parameter> Read(Node functionDeclarator)
	{
		var list = functionDeclarator.Field("parameters");
		if (list is null) return [];

		var parameters = new List<Parameter>();
		foreach (var node in list.Children) {
			if (node.Type is not ("parameter_declaration" or "optional_parameter_declaration")) continue;

			var defaultValue = node.Field("default_value");
			var name = node.Field("declarator") is { } declarator ? FirstIdentifier(declarator) : null;

			parameters.Add(name is not null
				? new Parameter(name.Str(), node.Slice(node.StartIndex, name.StartIndex).Trim(), defaultValue?.Str().Trim())
				: new Parameter("", UnnamedType(node, defaultValue), defaultValue?.Str().Trim()));
		}
		return parameters;
	}

	private static string UnnamedType(Node parameter, Node? defaultValue)
	{
		var end = defaultValue?.StartIndex ?? parameter.EndIndex;
		return parameter.Slice(parameter.StartIndex, end).Trim().TrimEnd('=').Trim();
	}

	private static Node? FirstIdentifier(Node node)
	{
		if (node.Type == "identifier") return node;
		return node.Children.Select(FirstIdentifier).FirstOrDefault(found => found is not null);
	}
}
