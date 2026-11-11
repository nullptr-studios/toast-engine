using System.Collections.Generic;
using System.Linq;

namespace editor.LuaEditor.Editing;

public static class TypeText {
	public static List<string> SplitTopLevel(string text) {
		var parts = new List<string>();
		var depth = 0;
		var start = 0;
		for (var i = 0; i < text.Length; i++) {
			switch (text[i]) {
				case '(' or '<' or '{' or '[': depth++; break;
				case ')' or '>' or '}' or ']': depth--; break;
				case ',' when depth == 0:
					parts.Add(text[start..i].Trim());
					start = i + 1;
					break;
			}
		}
		parts.Add(text[start..].Trim());
		return parts.Where(p => p.Length > 0).ToList();
	}
}
