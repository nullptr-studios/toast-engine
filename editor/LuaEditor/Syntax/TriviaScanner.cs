using System.Collections.Generic;
using Loretta.CodeAnalysis;
using Loretta.CodeAnalysis.Lua;
using Loretta.CodeAnalysis.Text;

namespace editor.LuaEditor.Syntax;

public enum TriviaLineKind { Comment, Annotation, Cast, Blank }

public sealed record TriviaLine(TriviaLineKind Kind, string Text, TextSpan Span);

public static class TriviaScanner {
	public static IReadOnlyList<TriviaLine> LeadingLines(SyntaxToken token) {
		var lines = new List<TriviaLine>();
		var lineHasContent = false;
		foreach (var trivia in token.LeadingTrivia) {
			var text = trivia.ToString();
			switch (trivia.Kind().ToString()) {
				case "WhitespaceTrivia":
					break;
				case "EndOfLineTrivia":
					if (!lineHasContent) lines.Add(new TriviaLine(TriviaLineKind.Blank, "", trivia.Span));
					lineHasContent = false;
					break;
				case "SingleLineCommentTrivia" or "MultiLineCommentTrivia" or "ShebangTrivia":
					lines.Add(new TriviaLine(KindOf(text), text.TrimEnd('\r', '\n'), trivia.Span));
					lineHasContent = true;
					break;
				default:
					break;
			}
		}
		return lines;
	}

	private static TriviaLineKind KindOf(string text) {
		if (text.StartsWith("---@cast", System.StringComparison.Ordinal)) return TriviaLineKind.Cast;
		if (text.StartsWith("---@", System.StringComparison.Ordinal)) return TriviaLineKind.Annotation;
		return TriviaLineKind.Comment;
	}
}
