using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Layout;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Editing;

public sealed record DocPin(string Name, string Type, string Color);

public sealed record NodeDoc(string Title, string Summary, IReadOnlyList<DocPin> Inputs, IReadOnlyList<DocPin> Outputs) {
	public static NodeDoc Of(GraphNode node, NodeCatalog catalog) {
		var title = NodeShape.TitleOf(node);
		var summary = node switch {
			IfStatement => "Runs the first branch whose condition is true; else runs the otherwise branch.",
			WhileStatement => "Repeats its body while the condition is true.",
			RepeatStatement => "Repeats its body until the condition is true.",
			NumericFor => "Counts from start to limit, in steps, running the body each time.",
			GenericFor => "Runs the body for each entry the iterator gives (pairs, ipairs, ...).",
			DoStatement => "A block with a scope of its own.",
			ReturnStatement => "Leaves the function, giving back its values.",
			BreakStatement => "Leaves the innermost loop.",
			SequenceStatement => "Runs its outputs one after the other.",
			LocalDecl => "Makes a local variable.",
			AssignStatement => "Stores a value in a variable or field.",
			FunctionDecl => "A function of this script.",
			FunctionEntry => "Where a function begins; its parameters come out here.",
			BinaryOp or UnaryOp => "An operator: computes a value from its inputs.",
			TableNode => "Makes a table.",
			IndexGet => "Reads an entry of a table by key.",
			MemberGet => "Reads a field of a table or object.",
			VariableGet => "Reads a variable.",
			CommentStatement => "A comment.",
			_ => ""
		};
		if (catalog.Entries.FirstOrDefault(e => e.Doc.Length > 0 && (e.Title == title || e.Name == title)) is { } entry) summary = entry.Doc;
		var inputs = node.Inputs.Where(p => p.Name.Length > 0).Select(p => new DocPin(p.Name, p.Expected is { } expected ? TypeFormatter.Display(expected) : "any", PinStyle.ColorOf(p.Expected ?? LuaType.Any))).ToList();
		var outputs = node.Outputs.Select(p => new DocPin(p.Name, TypeFormatter.Display(p.Type), PinStyle.ColorOf(p.Type))).ToList();
		return new NodeDoc(title, summary, inputs, outputs);
	}
}
