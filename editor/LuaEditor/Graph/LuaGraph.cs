using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Syntax;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Graph;

public sealed partial class LuaGraph {
	private readonly List<GraphNode> m_nodes = [];
	private readonly List<string> m_warnings = [];
	private int m_nextId = 1;

	private LuaGraph(LuaDocument document, TypeUniverse universe) {
		Document = document;
		Universe = universe;
		Module = new Block("module");
	}

	public LuaDocument Document { get; private set; }

	public TypeUniverse Universe { get; }

	public Block Module { get; }

	public IReadOnlyList<GraphNode> Nodes => m_nodes;

	public IReadOnlyList<string> Warnings => m_warnings;

	public IReadOnlyList<GraphDiagnostic> Diagnostics { get; private set; } = [];

	public IEnumerable<Wire> Wires => m_nodes.SelectMany(n => n.Inputs).Where(p => p.Source is not null).Select(p => new Wire(p.Source!, p));

	public IEnumerable<Statement> AllStatements() => m_nodes.OfType<Statement>();

	internal int Register(GraphNode node) {
		m_nodes.Add(node);
		return m_nextId++;
	}

	internal void Unregister(GraphNode node) => m_nodes.Remove(node);

	public void Analyze() => StackGuard.Run(() => {
		Typer.Run(this);
		return 0;
	});

	public SaveResult Save() => StackGuard.Run(() => {
		try {
			MaterializeSharedValues();
		} catch (InvalidOperationException e) {
			return new SaveResult(Document.Text, [], [new Diagnostic(DiagnosticSeverity.Error, "a value used in two places cannot be kept in a local there: " + e.Message, new SourceLocation("", 1))]);
		}
		return GraphSaver.Save(this);
	});

	internal void Warn(string message) => m_warnings.Add(message);

	internal void SetDiagnostics(IReadOnlyList<GraphDiagnostic> diagnostics) => Diagnostics = diagnostics;

	public static bool TryBuild(LuaDocument document, TypeUniverse universe, out LuaGraph? graph, out IReadOnlyList<Diagnostic> errors) {
		if (document.HasErrors) {
			graph = null;
			errors = document.Errors;
			return false;
		}
		try {
			var built = new LuaGraph(document, universe);
			StackGuard.Run(() => {
				new GraphBuilder(built, document).Build();
				return 0;
			});
			graph = built;
			errors = [];
			return true;
		} catch (Exception e) when (e is InvalidOperationException or ArgumentException or NullReferenceException or InvalidCastException) {
			graph = null;
			errors = [new Diagnostic(DiagnosticSeverity.Error, $"could not build the graph: {e.Message}", new SourceLocation("", 1))];
			return false;
		}
	}
}
