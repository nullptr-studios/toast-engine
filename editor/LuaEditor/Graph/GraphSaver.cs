using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Syntax;
using editor.LuaEditor.Types;
using Loretta.CodeAnalysis.Text;

namespace editor.LuaEditor.Graph;

public sealed record SaveResult(string Text, IReadOnlyList<TextEdit> Edits, IReadOnlyList<Diagnostic> Errors);

internal sealed record PartDef(TextSpan Span, Func<bool, int, string> Write, Func<IReadOnlyList<FunctionNode>> Closures);

internal static class GraphSaver {

	public static void Snapshot(LuaGraph graph) {
		var writer = new LuaWriter(graph.Document);
		foreach (var function in graph.Nodes.OfType<FunctionNode>()) function.OriginalParams = writer.ParamsText(function.Entry);
		foreach (var block in AllBlocks(graph)) block.Original = block.Statements.ToList();

		foreach (var statement in graph.AllStatements()) {
			switch (statement) {
				case CommentStatement comment:
					statement.Baseline = new StatementBaseline("K", [new PartBaseline(comment.LineSpan ?? default, string.Join("\n", comment.Lines))], []);
					break;
				case CastStatement cast:
					statement.Baseline = new StatementBaseline("Q", [new PartBaseline(cast.LineSpan ?? default, CastText(cast))], []);
					break;
				default:
					if (statement.Source is null) break;
					statement.Baseline = new StatementBaseline(ShapeOf(statement),
						PartsOf(statement, writer).Select(p => new PartBaseline(p.Span, p.Write(true, 0), p.Closures())).ToList(),
						statement.LeadingLines.ToList());
					break;
			}
		}
	}

	private static IEnumerable<Block> AllBlocks(LuaGraph graph)
		=> new[] { graph.Module }
			.Concat(graph.AllStatements().SelectMany(s => s.ChildBlocks))
			.Concat(graph.Nodes.OfType<FunctionNode>().Select(f => f.Entry.Body));

	private static string CastText(CastStatement cast) => $"{cast.Variable} {cast.TypeText}";

	private static string ShapeOf(Statement statement) => statement switch {
		LocalDecl l => $"L{l.Names.Count}:{l.Values.Count}",
		AssignStatement a => $"A{string.Join(",", a.Targets.Select(t => (int)t.Kind))}:{a.Values.Count}",
		CallStatement => "C",
		IfStatement i => $"I{i.Clauses.Count}:{i.Else is not null}",
		WhileStatement => "W",
		RepeatStatement => "R",
		NumericFor n => $"N{!n.Step.IsEmpty}",
		GenericFor g => $"G{g.Vars.Count}:{g.Exprs.Count}",
		DoStatement => "D",
		FunctionDecl f => $"F{f.IsLocal}:{f.Path.Count}:{f.IsMethod}:{f.Entry.Params.Count}:{f.Entry.IsVararg}",
		ReturnStatement r => $"T{r.Values.Count}",
		BreakStatement => "B",
		GotoStatement => "O",
		LabelStatement => "Y",
		_ => "X",
	};

	private static PartDef Expression(InputPin pin, LuaWriter writer)
		=> new(pin.Origin?.Span ?? default, (placeholder, level) => writer.ExpressionAt(pin, level, placeholder), () => ClosuresIn(pin));

	private static PartDef Text(TextSpan span, Func<string> text) => new(span, (_, _) => text(), () => []);

	private static IEnumerable<PartDef> ValueParts(IEnumerable<InputPin> pins, LuaWriter writer)
		=> pins.Where(p => !p.IsEmpty).Select(p => Expression(p, writer));

	private static List<PartDef> PartsOf(Statement statement, LuaWriter writer) {
		TextSpan Span(string key) => statement.Spans.GetValueOrDefault(key);
		var parts = new List<PartDef>();
		switch (statement) {
			case LocalDecl local:
				parts.Add(Text(Span("names"), () => writer.NamesText(local)));
				parts.AddRange(ValueParts(local.Values, writer));
				break;
			case AssignStatement assign:
				parts.Add(new PartDef(Span("targets"), (placeholder, level) => writer.Targets(assign, level, placeholder),
					() => assign.Targets.SelectMany(t => new[] { t.Object, t.Key }).Where(p => p is not null).SelectMany(p => ClosuresIn(p!)).ToList()));
				parts.AddRange(ValueParts(assign.Values, writer));
				break;
			case CallStatement call:
				parts.Add(Expression(call.Call, writer));
				break;
			case IfStatement @if:
				parts.AddRange(@if.Clauses.Select(c => Expression(c.Condition, writer)));
				break;
			case WhileStatement @while:
				parts.Add(Expression(@while.Condition, writer));
				break;
			case RepeatStatement repeat:
				parts.Add(Expression(repeat.Condition, writer));
				break;
			case NumericFor numeric:
				parts.Add(Text(Span("var"), () => numeric.Var.Name));
				parts.Add(Expression(numeric.Start, writer));
				parts.Add(Expression(numeric.Limit, writer));
				if (!numeric.Step.IsEmpty) parts.Add(Expression(numeric.Step, writer));
				break;
			case GenericFor generic:
				parts.Add(Text(Span("vars"), () => writer.ForVarsText(generic)));
				parts.AddRange(ValueParts(generic.Exprs, writer));
				break;
			case FunctionDecl function:
				parts.Add(Text(Span("name"), () => writer.FunctionNameText(function)));
				parts.Add(Text(function.Entry.ParamsSpan ?? default, () => writer.ParamsText(function.Entry)));
				break;
			case ReturnStatement @return:
				parts.AddRange(ValueParts(@return.Values, writer));
				break;
			case GotoStatement @goto:
				parts.Add(Text(Span("label"), () => @goto.Label));
				break;
			case LabelStatement label:
				parts.Add(Text(Span("name"), () => label.Name));
				break;
			case RawStatement raw:
				parts.Add(Text(statement.Source?.Span ?? default, () => raw.Text));
				break;
		}
		return parts;
	}

	private static IReadOnlyList<FunctionNode> ClosuresIn(InputPin pin) => LuaGraph.ClosuresIn(pin).ToList();

	public static SaveResult Save(LuaGraph graph) => StackGuard.Run(() => SaveCore(graph));

	private static SaveResult Failure(LuaGraph graph, IReadOnlyList<TextEdit> edits, string message)
		=> new(graph.Document.Text, edits, [new Diagnostic(DiagnosticSeverity.Error, message, new SourceLocation("", 1))]);

	private static SaveResult SaveCore(LuaGraph graph) {
		var doc = graph.Document;
		var saver = new Saver(graph);
		try {
			saver.SaveBlock(graph.Module);
			if (saver.Edits.Count == 0) return new SaveResult(doc.Text, saver.Edits, []);
			var text = doc.Apply(saver.Edits);

			var check = LuaDocument.Parse(text);
			if (check.HasErrors)
				return new SaveResult(doc.Text, saver.Edits, check.Errors.Select(e => e with { Message = "the saved text would not parse: " + e.Message }).ToList());

			// reject saves that change which declaration a name refers to
			if (!LuaGraph.TryBuild(check, graph.Universe, out var rebuilt, out var rebuildErrors))
				return Failure(graph, saver.Edits, "the saved text could not be read back: " + string.Join("; ", rebuildErrors.Select(e => e.Message)));
			var expected = GraphSignature.Of(graph);
			var actual = GraphSignature.Of(rebuilt!);
			if (expected != actual) return Failure(graph, saver.Edits, "the saved text would mean something different from the graph: " + FirstDifference(expected, actual));
			return new SaveResult(text, saver.Edits, []);
		} catch (Exception e) when (e is ArgumentException or InvalidOperationException or ArgumentOutOfRangeException or NullReferenceException or KeyNotFoundException) {
			return Failure(graph, saver.Edits, "could not save: " + e.Message);
		}
	}

	private static string FirstDifference(string expected, string actual) {
		var a = expected.Split('\n');
		var b = actual.Split('\n');
		for (var i = 0; i < Math.Max(a.Length, b.Length); i++) {
			var left = i < a.Length ? a[i] : "(end)";
			var right = i < b.Length ? b[i] : "(end)";
			if (left != right) return $"graph \"{Shorten(left)}\" but text \"{Shorten(right)}\"";
		}
		return "unknown";
	}

	private static string Shorten(string text) => text.Length <= 120 ? text : text[..120] + "...";

	private sealed class Saver {
		private readonly LuaGraph m_graph;
		private readonly LuaDocument m_doc;
		private readonly LuaWriter m_writer;
		private readonly string m_text;

		public Saver(LuaGraph graph) {
			m_graph = graph;
			m_doc = graph.Document;
			m_text = m_doc.Text;
			m_writer = new LuaWriter(m_doc) { VerbatimClosure = ClosureText, VerbatimStatement = StatementText };
		}

		public List<TextEdit> Edits { get; } = [];

		private string Eol => m_doc.Eol;

		private int LevelOf(string indent) {
			var unit = m_doc.IndentUnit;
			if (unit == "\t") return indent.Count(c => c == '\t');
			return indent.Length / Math.Max(1, unit.Length);
		}

		private bool OnlyWhitespaceBefore(int position) {
			for (var i = m_doc.LineStart(position); i < position; i++)
				if (m_text[i] is not (' ' or '\t')) return false;
			return true;
		}

		private int LineStartIfAlone(int position) => OnlyWhitespaceBefore(position) ? m_doc.LineStart(position) : position;

		private int SourceStart(Statement s) => s.Source is not null ? s.Source.Span.Start : (s switch {
			CommentStatement c => c.LineSpan!.Value.Start,
			CastStatement c => c.LineSpan!.Value.Start,
			_ => throw new InvalidOperationException($"{s.GetType().Name} has no place in the text"),
		});

		private int StartOf(Statement s) => LineStartIfAlone(s.LeadingLinesSpan?.Start ?? SourceStart(s));

		private int EndOfLine(int position) {
			var i = position;
			while (i < m_text.Length && m_text[i] != '\n') i++;
			return i < m_text.Length ? i + 1 : i;
		}

		private int EndOf(Statement s) => s.Source is not null ? s.Source.FullSpan.End : EndOfLine(s switch {
			CommentStatement c => c.LineSpan!.Value.End,
			CastStatement c => c.LineSpan!.Value.End,
			_ => throw new InvalidOperationException(),
		});

		private int LevelAt(Statement s) => LevelOf(m_doc.IndentOfLineAt(SourceStart(s)));

		private string? ClosureText(FunctionNode function, int level) {
			if (function.SourceSpan is not { } span || function.OriginalParams is null) return null;
			if (m_writer.ParamsText(function.Entry) != function.OriginalParams) return null;
			if (LevelOf(m_doc.IndentOfLineAt(span.Start)) != level) return null;
			return BlockUnchanged(function.Entry.Body) ? m_text.Substring(span.Start, span.Length) : null;
		}

		private string? StatementText(Statement statement, int level) {
			if (statement.Baseline is null || statement.Source is null || statement is CommentStatement or CastStatement or SequenceStatement) return null;
			var start = SourceStart(statement);
			if (!OnlyWhitespaceBefore(start) || LevelOf(m_doc.IndentOfLineAt(start)) != level) return null;
			if (!StatementUnchanged(statement)) return null;

			var from = StartOf(statement);
			while (from > 0) {
				var previous = m_doc.LineStart(from - 1);
				if (m_text[previous..from].Trim().Length != 0) break;
				from = previous;
			}
			var to = statement.Source.FullSpan.End;
			while (to > from && m_text[to - 1] is '\n' or '\r') to--;
			return m_text[from..to];
		}

		private bool StatementUnchanged(Statement statement) {
			var probe = new Saver(m_graph);
			try {
				probe.SaveStatement(statement);
			} catch (InvalidOperationException) {
				return false;
			}
			return probe.Edits.Count == 0;
		}

		private bool BlockUnchanged(Block block) {
			var probe = new Saver(m_graph);
			try {
				probe.SaveBlock(block);
			} catch (InvalidOperationException) {
				return false;
			}
			return probe.Edits.Count == 0;
		}

		public void SaveBlock(Block block) {
			var original = block.Original;
			var current = LuaGraph.Flatten(block.Statements);
			var originalIndex = new Dictionary<Statement, int>();
			for (var i = 0; i < original.Count; i++) originalIndex[original[i]] = i;

			var kept = current.Where(originalIndex.ContainsKey).ToList();
			var stable = LongestIncreasing(kept.Select(s => originalIndex[s]).ToList()).Select(i => kept[i]).ToHashSet();

			if (original.Count == 0) {
				if (current.Count > 0) FillEmptyBlock(block);
				return;
			}

			// inserts must precede deletions at the same offset
			Statement? anchor = null;
			var indent = BlockIndent(block);
			foreach (var statement in current) {
				if (stable.Contains(statement)) {
					anchor = statement;
					continue;
				}
				Edits.Add(InsertAfter(block, anchor, statement, indent));
			}

			for (var i = 0; i < current.Count; i++) {
				var statement = current[i];
				if (!stable.Contains(statement)) continue;
				var originalPrevious = originalIndex[statement] > 0 ? original[originalIndex[statement] - 1] : null;
				var previous = i > 0 ? current[i - 1] : null;

				// a blank line keeps this comment separate from the next statement
				if (previous is CommentStatement && stable.Contains(previous) && previous != originalPrevious) Edits.Add(TextEdit.Insert(StartOf(statement), Eol));

				SaveStatement(statement);

				// without a separator this becomes a call on the previous expression
				if (previous is not null && previous != originalPrevious && m_text[SourceStart(statement)] == '(')
					Edits.Add(TextEdit.Insert(SourceStart(statement), ";"));
			}

			foreach (var statement in original.Where(s => !stable.Contains(s))) Edits.Add(new TextEdit(StartOf(statement), EndOf(statement) - StartOf(statement), ""));
		}

		private string BlockIndent(Block block) {
			foreach (var statement in block.Original) {
				var start = SourceStart(statement);
				if (OnlyWhitespaceBefore(start)) return m_doc.IndentOfLineAt(start);
			}
			if (block.Owner is null) return "";
			return m_doc.IndentOfLineAt(Math.Max(0, block.OpenEnd - 1)) + m_doc.IndentUnit;
		}

		private string Written(Statement statement, string indent) => m_writer.StatementWithLeading(statement, LevelOf(indent));

		private TextEdit InsertAfter(Block block, Statement? anchor, Statement statement, string indent) {
			var text = Written(statement, indent);
			if (text.Length == 0) return TextEdit.Insert(0, "");
			// a blank line keeps this comment separate from the next statement
			var gap = statement is CommentStatement ? Eol : "";

			if (anchor is not null) {
				var position = EndOf(anchor);
				// a blank line keeps this comment separate from the next statement
				var lead = anchor is CommentStatement ? Eol : "";
				if (position == 0 || m_text[position - 1] == '\n') return TextEdit.Insert(position, lead + text + Eol + gap);
				var followedByLine = position >= m_text.Length || m_text[position] is '\n' or '\r';
				return TextEdit.Insert(position, Eol + lead + text + (followedByLine ? "" : Eol));
			}

			var first = block.Original[0];
			var at = StartOf(first);
			if (at == SourceStart(first) && !OnlyWhitespaceBefore(at)) return TextEdit.Insert(at, text.TrimStart(' ', '\t') + Eol + indent);
			return TextEdit.Insert(at, text + Eol + gap);
		}

		private void FillEmptyBlock(Block block) {
			var indent = BlockIndent(block);
			var body = m_writer.Statements(LuaGraph.Flatten(block.Statements), LevelOf(indent));
			if (body.Length == 0) return;
			if (block.Owner is null) {
				Edits.Add(new TextEdit(block.OpenEnd, block.CloseStart - block.OpenEnd, body + Eol));
				return;
			}
			var parentIndent = m_doc.IndentOfLineAt(Math.Max(0, block.OpenEnd - 1));
			Edits.Add(new TextEdit(block.OpenEnd, block.CloseStart - block.OpenEnd, Eol + body + Eol + parentIndent));
		}

		private static List<int> LongestIncreasing(List<int> values) {
			var best = new int[values.Count];
			var previous = new int[values.Count];
			var end = -1;
			for (var i = 0; i < values.Count; i++) {
				best[i] = 1;
				previous[i] = -1;
				for (var j = 0; j < i; j++)
					if (values[j] < values[i] && best[j] + 1 > best[i]) {
						best[i] = best[j] + 1;
						previous[i] = j;
					}
				if (end < 0 || best[i] > best[end]) end = i;
			}
			var result = new List<int>();
			for (var i = end; i >= 0; i = previous[i]) result.Add(i);
			result.Reverse();
			return result;
		}

		private static bool SameClosures(IReadOnlyList<FunctionNode> current, IReadOnlyList<FunctionNode>? baseline)
			=> baseline is not null && current.Count == baseline.Count && current.Zip(baseline).All(p => ReferenceEquals(p.First, p.Second));

		public void SaveStatement(Statement statement) {
			var baseline = statement.Baseline;
			if (baseline is null) throw new InvalidOperationException($"{statement.GetType().Name} was not part of the loaded file");

			switch (statement) {
				case CommentStatement comment: {
					if (string.Join("\n", comment.Lines) != baseline.Parts[0].Text) {
						var span = comment.LineSpan!.Value;
						Edits.Add(new TextEdit(span.Start, span.Length, string.Join(Eol + m_doc.IndentOfLineAt(span.Start), comment.Lines)));
					}
					return;
				}
				case CastStatement cast: {
					if (CastText(cast) != baseline.Parts[0].Text) {
						var span = cast.LineSpan!.Value;
						Edits.Add(new TextEdit(span.Start, span.Length, $"---@cast {CastText(cast)}".TrimEnd()));
					}
					return;
				}
			}

			SaveLeadingLines(statement, baseline);
			var level = LevelAt(statement);
			var current = PartsOf(statement, m_writer);
			if (ShapeOf(statement) != baseline.Shape || current.Count != baseline.Parts.Count) {
				var source = statement.Source!;
				Edits.Add(new TextEdit(source.Span.Start, source.Span.Length, m_writer.Statement(statement, level)));
				return;
			}

			for (var i = 0; i < current.Count; i++) {
				var closures = current[i].Closures();
				if (current[i].Write(true, 0) == baseline.Parts[i].Text && SameClosures(closures, baseline.Parts[i].Closures)) {
					foreach (var closure in closures) SaveBlock(closure.Entry.Body);
					continue;
				}
				var span = baseline.Parts[i].Span;
				var text = current[i].Write(false, level);
				// without a separator this becomes a call on the previous expression
				if (i == 0 && statement is CallStatement or AssignStatement && text.StartsWith('(') && !baseline.Parts[0].Text.StartsWith('(')) text = ";" + text;
				Edits.Add(new TextEdit(span.Start, span.Length, text));
			}
			foreach (var child in statement.ChildBlocks) SaveBlock(child);
		}

		private void SaveLeadingLines(Statement statement, StatementBaseline baseline) {
			if (statement.LeadingLines.SequenceEqual(baseline.LeadingLines)) return;
			var start = SourceStart(statement);
			var indent = m_doc.IndentOfLineAt(start);
			if (statement.LeadingLinesSpan is { } span) {
				if (statement.LeadingLines.Count > 0) {
					Edits.Add(new TextEdit(span.Start, span.Length, string.Join(Eol + indent, statement.LeadingLines)));
				} else {
					var from = LineStartIfAlone(span.Start);
					var to = LineStartIfAlone(start);
					if (to > from) Edits.Add(new TextEdit(from, to - from, ""));
				}
			} else if (statement.LeadingLines.Count > 0) {
				if (!OnlyWhitespaceBefore(start))
					throw new InvalidOperationException("a comment cannot go above a statement that shares its line with other code");
				var position = m_doc.LineStart(start);
				Edits.Add(TextEdit.Insert(position, string.Concat(statement.LeadingLines.Select(l => indent + l + Eol))));
			}
		}
	}
}
