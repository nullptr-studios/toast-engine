using System.Text;

namespace ReflectionGenerator.Parsing;

internal static class TemplateArgs
{
	// "Signal<int, Foo<A, B>>" gives ["int", "Foo<A, B>"]
	public static IReadOnlyList<string> Split(string typeName)
	{
		var start = typeName.IndexOf('<');
		var end = typeName.LastIndexOf('>');
		if (start < 0 || end < 0 || start >= end) return [];

		var args = new List<string>();
		var current = new StringBuilder();
		var depth = 0;

		void Flush()
		{
			var text = current.ToString().Trim();
			if (text.Length > 0) args.Add(text);
			current.Clear();
		}

		foreach (var c in typeName[(start + 1)..end]) {
			switch (c) {
				case '<': depth++; current.Append(c); break;
				case '>': depth--; current.Append(c); break;
				case ',' when depth == 0: Flush(); break;
				default: current.Append(c); break;
			}
		}
		Flush();
		return args;
	}
}
