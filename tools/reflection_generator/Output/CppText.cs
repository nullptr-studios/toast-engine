using ReflectionGenerator.Model;

namespace ReflectionGenerator.Output;

internal static class CppText
{
	public static string Attributes(AttributeList attributes)
	{
		var entries = attributes.ByName().Select(a => "{\"" + a.Key + "\", {" + string.Join(", ", a.Value.Select(v => "\"" + v + "\"")) + "}}");
		return "{{" + string.Join(", ", entries) + "}}";
	}

	public static string TypeIds(IEnumerable<string> types) =>
		"{{" + string.Join(", ", types.Select(t => $"&typeid(std::decay_t<{t}>)")) + "}}";

	public static string DefaultValue(string? value) =>
		value is null ? "std::nullopt" : $"std::optional<std::string_view>{{\"{Escape(value)}\"}}";

	private static string Escape(string text) => text.Replace("\\", "\\\\").Replace("\"", "\\\"");
}
