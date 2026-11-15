using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Syntax;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Graph;

public sealed record PasteResult(IReadOnlyList<Statement> Statements, IReadOnlyList<Diagnostic> Errors);

public static class Fragments {
	public static string Copy(IEnumerable<Statement> statements) {
		var list = statements.ToList();
		if (list.Count == 0) return "";
		var graph = list[0].Graph;
		var text = new LuaWriter(graph.Document).Statements(list, 0);
		return text.Length == 0 ? "" : text + graph.Document.Eol;
	}

	public static string? CopyExpression(ExprNode root) {
		var graph = root.Graph;
		var holder = new ReturnStatement(graph);
		var pin = holder.AddValue();
		try {
			pin.Connect(root.Result);
			return new LuaWriter(graph.Document).Expression(pin);
		} catch (InvalidOperationException) {
			return null;
		} finally {
			pin.Clear();
			graph.Unregister(holder);
		}
	}

	public static List<ExprNode> BuildLoose(LuaGraph graph, string expression) {
		var document = LuaDocument.Parse("return " + expression);
		if (document.HasErrors) return [];
		var before = graph.Nodes.Count;
		try {
			var built = StackGuard.Run(() => new GraphBuilder(graph, document).BuildFragment(LuaGraph.VisibleFromBlock(graph.Module, graph.Module.Statements.Count)));
			if (built is [ReturnStatement { Values: [{ Source: { Binding: null, Owner: ExprNode root } } pin] } holder]) {
				pin.Clear();
				graph.Unregister(holder);
				var nodes = graph.Nodes.Skip(before).OfType<ExprNode>().Where(n => n != root).Prepend(root).ToList();
				return nodes;
			}
		} catch (Exception e) when (e is InvalidOperationException or ArgumentException or NullReferenceException) {
		}
		graph.ForgetNodesAfter(before);
		return [];
	}

	public static PasteResult Paste(LuaGraph graph, string luaText, Block into, int index) {
		if (index < 0 || index > into.Statements.Count) return Failure("the place to paste at is outside the block");
		var document = LuaDocument.Parse(luaText);
		if (document.HasErrors) return new PasteResult([], document.Errors);

		List<Statement> built;
		try {
			var nodesBefore = graph.Nodes.Count;
			try {
				built = StackGuard.Run(() => new GraphBuilder(graph, document).BuildFragment(LuaGraph.VisibleFromBlock(into, index)));
			} catch {
				graph.ForgetNodesAfter(nodesBefore);
				throw;
			}
		} catch (Exception e) when (e is InvalidOperationException or ArgumentException or NullReferenceException) {
			return Failure("could not read the pasted text: " + e.Message);
		}

		var inserted = new List<Statement>();
		try {
			foreach (var statement in built) {
				graph.Insert(into, index + inserted.Count, statement);
				inserted.Add(statement);
			}
		} catch (Exception e) when (e is InvalidOperationException or ArgumentException) {
			foreach (var statement in inserted) graph.Discard(statement);
			foreach (var statement in built.Skip(inserted.Count)) graph.Discard(statement);
			return Failure(e.Message);
		}
		return new PasteResult(built, []);

		static PasteResult Failure(string message) => new([], [new Diagnostic(DiagnosticSeverity.Error, message, new SourceLocation("", 1))]);
	}
}
