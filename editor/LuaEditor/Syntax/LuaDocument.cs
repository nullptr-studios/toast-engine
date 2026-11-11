using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Loretta.CodeAnalysis;
using Loretta.CodeAnalysis.Lua;
using Loretta.CodeAnalysis.Lua.Syntax;
using TypesDiagnostic = editor.LuaEditor.Types.Diagnostic;
using TypesSeverity = editor.LuaEditor.Types.DiagnosticSeverity;
using TypesLocation = editor.LuaEditor.Types.SourceLocation;

namespace editor.LuaEditor.Syntax;

public sealed class LuaDocument {
	private static readonly LuaParseOptions s_options = new(LuaSyntaxOptions.Lua54);

	private LuaDocument(string text, SyntaxTree tree, IReadOnlyList<TypesDiagnostic> errors) {
		Text = text;
		Tree = tree;
		Root = (CompilationUnitSyntax)tree.GetRoot();
		Errors = errors;
		Eol = DetectEol(text);
		IndentUnit = DetectIndent(text);
	}

	public string Text { get; }
	public SyntaxTree Tree { get; }
	public CompilationUnitSyntax Root { get; }
	public IReadOnlyList<TypesDiagnostic> Errors { get; }
	public bool HasErrors => Errors.Count > 0;

	public string Eol { get; }

	public string IndentUnit { get; }

	public static LuaDocument Parse(string text) {
		return Graph.StackGuard.Run(() => ParseCore(text));
	}

	private static LuaDocument ParseCore(string text) {
		var tree = LuaSyntaxTree.ParseText(text, s_options);
		var errors = tree.GetDiagnostics()
			.Where(d => d.Severity == Loretta.CodeAnalysis.DiagnosticSeverity.Error)
			.Select(d => new TypesDiagnostic(TypesSeverity.Error, d.GetMessage(), new TypesLocation("", d.Location.GetLineSpan().StartLinePosition.Line + 1)))
			.ToList();
		return new LuaDocument(text, tree, errors);
	}

	public string IndentOfLineAt(int position) {
		var start = LineStart(position);
		var end = start;
		while (end < Text.Length && Text[end] is ' ' or '\t') end++;
		return Text[start..end];
	}

	public int LineStart(int position) {
		var i = Math.Min(position, Text.Length);
		while (i > 0 && Text[i - 1] != '\n') i--;
		return i;
	}

	public int LineOf(int position) {
		var line = 1;
		for (var i = 0; i < Math.Min(position, Text.Length); i++)
			if (Text[i] == '\n') line++;
		return line;
	}

	public string Apply(IEnumerable<TextEdit> edits) {
		var sorted = edits.Select((e, i) => (Edit: e, Order: i)).OrderBy(x => x.Edit.Start).ThenBy(x => x.Edit.Length > 0 ? 1 : 0).ThenBy(x => x.Order).Select(x => x.Edit).ToList();
		var sb = new StringBuilder(Text.Length + 64);
		var cursor = 0;
		foreach (var edit in sorted) {
			if (edit.Start < cursor) throw new ArgumentException($"edits overlap at {edit.Start}");
			if (edit.End > Text.Length) throw new ArgumentException($"edit at {edit.Start} runs past the end of the text");
			sb.Append(Text, cursor, edit.Start - cursor).Append(edit.Text);
			cursor = edit.End;
		}
		return sb.Append(Text, cursor, Text.Length - cursor).ToString();
	}

	private static string DetectEol(string text) {
		var crlf = 0;
		var lf = 0;
		for (var i = 0; i < text.Length; i++) {
			if (text[i] != '\n') continue;
			if (i > 0 && text[i - 1] == '\r') crlf++;
			else lf++;
		}
		return crlf > lf ? "\r\n" : "\n";
	}

	private static string DetectIndent(string text) {
		var tabLines = 0;
		var widths = new Dictionary<int, int>();
		foreach (var raw in text.Split('\n')) {
			var line = raw.TrimEnd('\r');
			if (line.Trim().Length == 0) continue;
			if (line[0] == '\t') {
				tabLines++;
				continue;
			}
			var spaces = 0;
			while (spaces < line.Length && line[spaces] == ' ') spaces++;
			if (spaces > 0) widths[spaces] = widths.GetValueOrDefault(spaces) + 1;
		}
		if (tabLines > widths.Values.Sum()) return "\t";
		// continuation indentation should not set the block width
		var common = widths.Where(w => w.Value > 1).Select(w => w.Key).DefaultIfEmpty(0).Min();
		return common > 0 ? new string(' ', common) : "    ";
	}
}
