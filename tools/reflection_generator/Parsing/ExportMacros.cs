namespace ReflectionGenerator.Parsing;

internal static class ExportMacros
{
	// tree-sitter reads TOAST_API and declspec as identifiers and then loses the field that follows
	public static string Strip(string source) => source
		.Replace("TOAST_API ", "")
		.Replace("__declspec(dllexport) ", "")
		.Replace("__declspec(dllimport) ", "");
}
