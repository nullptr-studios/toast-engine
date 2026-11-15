namespace ReflectionGenerator;

internal static class Strings
{
	public static string Qualify(string? scope, string name) => scope is null ? name : $"{scope}::{name}";

	// last segment of a qualified name
	public static string Bare(this string name)
	{
		var cut = name.LastIndexOf("::", StringComparison.Ordinal);
		return cut < 0 ? name : name[(cut + 2)..];
	}

	public static string WithoutPrefix(this string text, string prefix) =>
		text.StartsWith(prefix, StringComparison.Ordinal) ? text[prefix.Length..] : text;

	// strips the prefix as many times as it repeats
	public static string TrimStartText(this string text, string prefix)
	{
		while (text.StartsWith(prefix, StringComparison.Ordinal)) {
			text = text[prefix.Length..];
		}
		return text;
	}

	public static string WithoutWhitespace(this string text) => string.Concat(text.Where(c => !char.IsWhiteSpace(c)));
}
