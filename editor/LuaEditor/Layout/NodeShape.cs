using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Layout;

public enum SlotKind { ExecIn, ExecOut, DataIn, DataOut }

public sealed record Slot(SlotKind Kind, int Row, string Label, GraphNode Node, InputPin? In = null, OutputPin? Out = null, Block? Child = null, LuaType? Type = null, bool IsCallableReference = false) {
	public bool IsLeft => Kind is SlotKind.ExecIn or SlotKind.DataIn;
	public bool IsExec => Kind is SlotKind.ExecIn or SlotKind.ExecOut;
}

public enum NodeLook { Event, Flow, Call, Variable, Operator, Value, Comment, Raw }

public static class NodeShape {
	public const string ContinuationLabel = "next";

	public static bool HasExecutionIn(GraphNode node) => node is Statement and not (CommentStatement or FunctionDecl or LabelStatement);

	public static bool HasContinuation(Statement s) => s is not (ReturnStatement or BreakStatement or GotoStatement or CommentStatement or FunctionDecl);

	public static string? TableLabel(InputPin pin) {
		if (pin.Owner is not TableNode table || table.Fields.FirstOrDefault(f => f.Value == pin) is not { Kind: TableFieldKind.Keyed, Key: { Source: null } key }) return null;
		return "[" + KeyText(key) + "]  ";
	}

	public static string KeyText(InputPin key) => key.Constant is { Kind: ConstantKind.String, Raw: var raw } && raw.Length >= 2 ? raw[1..^1] : key.Constant?.Raw ?? "key";

	public static IReadOnlyList<Slot> SlotsOf(GraphNode node) {

		var slots = new List<Slot>();
		var left = 0;
		var right = 0;

		if (node is Statement statement && HasExecutionIn(node)) slots.Add(new Slot(SlotKind.ExecIn, left++, "", node));

		foreach (var pin in InlineRules.VisibleInputs(node)) {
			if (pin.Owner is Statement && pin.Name == "table" && node is FunctionDecl d && d.Path.Count < 2) continue;
			slots.Add(new Slot(SlotKind.DataIn, left++, TableLabel(pin) ?? pin.Name, node, In: pin, Type: pin.Expected));
		}

		if (node is FunctionEntry entry) slots.Add(new Slot(SlotKind.ExecOut, right++, "", node, Child: entry.Body));
		if (node is Statement s) {
			var roles = s is FunctionDecl ? [] : s.ChildBlocks.ToList();
			foreach (var block in roles) slots.Add(new Slot(SlotKind.ExecOut, right++, RoleLabel(s, block), node, Child: block));
			if (HasContinuation(s)) slots.Add(new Slot(SlotKind.ExecOut, right++, roles.Count > 0 ? ContinuationLabel : "", node));
		}

		if (node is FunctionDecl declaration) {
			slots.Add(new Slot(SlotKind.ExecOut, right++, "", node, Child: declaration.Entry.Body));
			foreach (var output in declaration.Entry.Outputs)
				slots.Add(new Slot(SlotKind.DataOut, right++, output.Name, node, Out: output, Type: output.Type));
		}
		foreach (var output in node.Outputs) {
			if (node is CallStatement || InlineRules.IsMerged(node)) break;
			slots.Add(new Slot(SlotKind.DataOut, right++, output.Name, node, Out: output, Type: output.Type));
		}
		var call = CallOf(node);
		if (node is CallStatement && call is { Style: CallStyle.Function } &&
			(LocalCalleeOf(node) is not null || call.Target.Source?.Owner is MemberGet))
			foreach (var output in call.Outputs.Take(call.ReturnCount))
				slots.Add(new Slot(SlotKind.DataOut, right++, output.Name, node, Out: output, Type: output.Type));
		if (call is { Style: CallStyle.Function, Target.Source: { } callable } &&
			(callable.Owner is MemberGet || LocalCalleeOf(node) is not null))
			slots.Add(new Slot(SlotKind.DataOut, right++, "function", node, Out: callable, Type: callable.Type, IsCallableReference: true));

		if (node.IsExpanded && CalleeOf(node) is { } callee) {
			var names = callee.Entry.Params.Select(p => p.Name).ToList();
			var index = 0;
			for (var i = 0; i < slots.Count; i++) {
				if (slots[i].Kind != SlotKind.DataIn) continue;
				if (index < names.Count) slots[i] = slots[i] with { Label = names[index] };
				index++;
			}
		}
		return slots;

	}

	public static int SlotRows(GraphNode node) {
		var slots = SlotsOf(node);
		return slots.Count == 0 ? 1 : slots.Max(s => s.Row) + 1;
	}

	public static int Rows(GraphNode node) => SlotRows(node) + (node is TableNode ? 1 : 0);

	public static (FunctionDecl Callee, double InnerWidth, double InnerHeight)? ExpansionOf(GraphNode node) {
		if (!node.IsExpanded || CalleeOf(node) is not { } callee) return null;
		var statements = callee.Entry.Body.Statements.Count(s => s is not CommentStatement);
		return (callee, 440, Math.Clamp(150 + 22 * Math.Min(statements, 10), 170, 380));
	}

	public static ReturnStatement? TrailingReturn(Block body) => body.Statements.LastOrDefault(s => !s.IsFloating && s is not CommentStatement) as ReturnStatement;

	public static CallNode? CallOf(GraphNode node) => node switch {
		CallNode call => call,
		CallStatement statement => statement.CallNode,
		LocalDecl { Values: [{ Source.Owner: CallNode call }] } local when InlineRules.HostOf(call) == local => call,
		_ => null
	};

	public static Binding? BindingOf(OutputPin? pin) => pin?.Binding ?? (pin?.Owner as VariableGet)?.Binding;

	public static FunctionDecl? LocalCalleeOf(GraphNode node) {
		var call = CallOf(node);
		return call is { Style: CallStyle.Function } && BindingOf(call.Target.Source) is { IsMutable: false, Declarer: FunctionDecl { IsLocal: true } local }
			? local : null;
	}

	public static FunctionDecl? CalleeOf(GraphNode node) {
		if (LocalCalleeOf(node) is { } local) return local;
		var call = CallOf(node);
		if (call is null || ScriptInfo.Find(node.Graph) is not { } script) return null;
		var member = call.Target.Source?.Owner as MemberGet;
		var receiver = call.Style == CallStyle.Method ? call.Target : member?.Object;
		if (receiver is null || !script.IsTable(receiver) && BindingOf(receiver.Source)?.Kind != BindingKind.Self) return null;
		var name = call.Style == CallStyle.Method ? call.Method : member?.Name;
		return script.Methods.FirstOrDefault(d => d.Name == name);
	}

	public static IReadOnlyList<string> ExpansionLines(GraphNode node) {
		if (!node.IsExpanded || CalleeOf(node) is not { } callee) return [];
		var lines = new List<string> { callee.Name + "(" + string.Join(", ", callee.Entry.Params.Select(p => p.Name)) + ")" };
		var statements = callee.Entry.Body.Statements.Where(s => s is not CommentStatement).ToList();
		lines.AddRange(statements.Take(6).Select(s => "• " + TitleOf(s)));
		if (statements.Count > 6) lines.Add("… " + (statements.Count - 6) + " more");
		return lines;
	}

	public static Slot? SlotOf(InputPin pin) {
		var drawn = InlineRules.Drawn(pin.Owner);
		return SlotsOf(drawn).FirstOrDefault(s => s.In == pin);
	}

	public static Slot? SlotOf(OutputPin pin) {
		var drawn = InlineRules.Drawn(pin.Owner);
		return SlotsOf(drawn).FirstOrDefault(s => s.Out == pin);
	}

	public static string RoleLabel(Statement owner, Block block) {
		if (owner is IfStatement i) {
			var index = i.Clauses.FindIndex(c => c.Body == block);
			return index switch { 0 => "then", > 0 => "elseif", _ => "else" };
		}
		if (owner is SequenceStatement q) return "then " + q.Branches.IndexOf(block);
		return owner is NumericFor or GenericFor or WhileStatement ? "loop" : "do";
	}

	public static NodeLook LookOf(GraphNode node) => node switch {
		FunctionDecl or FunctionEntry or FunctionNode => NodeLook.Event,
		CommentStatement => NodeLook.Comment,
		RawStatement or RawExpression => NodeLook.Raw,
		IfStatement or WhileStatement or RepeatStatement or NumericFor or GenericFor or DoStatement or SequenceStatement
			or ReturnStatement or BreakStatement or GotoStatement or LabelStatement => NodeLook.Flow,
		LocalDecl local when CallOf(local) is not null => NodeLook.Call,
		LocalDecl or AssignStatement or CastStatement or VariableGet => NodeLook.Variable,
		BinaryOp or UnaryOp or ParenNode => NodeLook.Operator,
		CallStatement or CallNode => NodeLook.Call,
		_ => NodeLook.Value
	};

	public static string TitleOf(GraphNode node) {
		if (InlineRules.Title(node) is { } call) return call;
		return node switch {
			FunctionDecl d => (d.IsLocal ? "local function " : d.IsMethod ? "method " : "function ") + d.Name,
			FunctionEntry e => "Entry",
			LocalDecl l => "local " + string.Join(", ", l.Names.Select(n => n.Name)),
			AssignStatement a => "Set " + string.Join(", ", a.Targets.Select(t => (t.Object is { } o && InlineRules.ImplicitObjectName(o) is { } owner ? owner + "." : "") + (t.Name ?? "[ ]"))),
			IfStatement => "if",
			WhileStatement => "while",
			RepeatStatement => "repeat until",
			NumericFor f => "for " + f.Var.Name + " =",
			GenericFor g => "for " + string.Join(", ", g.Vars.Select(v => v.Name)) + " in",
			DoStatement => "do",
			ReturnStatement => "return",
			BreakStatement => "break",
			GotoStatement g => "goto " + g.Label,
			LabelStatement l => "::" + l.Name + "::",
			CastStatement c => "---@cast " + c.Variable + " " + c.TypeText,
			CommentStatement c => string.Join("  ", c.Lines.Select(l => l.TrimStart('-', ' ', '\t'))),
			SequenceStatement => "Sequence",
			RawStatement r => "Raw: " + r.Text,
			BinaryOp b => OperatorName(b.Op),
			UnaryOp u => u.Op switch { UnaryKind.Neg => "-", UnaryKind.Not => "not", UnaryKind.Len => "#", _ => "~" },
			ParenNode => "( )",
			VariableGet v => v.Binding.Name,
			VarargNode => "...",
			IndexGet => "[ ]",
			MemberGet m => "." + m.Name,
TableNode table => table.Flavor switch { TableFlavor.Array => "Array", TableFlavor.Dictionary => "Dictionary", _ => "Table" },
			FunctionNode f => "function(" + string.Join(", ", f.Entry.Params.Select(p => p.Name)) + ")",
			RawExpression r => r.Text,
			GlobalGet g => g.Name,
			_ => node.GetType().Name
		};
	}

	public static string OperatorName(BinaryKind op) => op switch {
		BinaryKind.Add => "+", BinaryKind.Sub => "-", BinaryKind.Mul => "*", BinaryKind.Div => "/", BinaryKind.FloorDiv => "//",
		BinaryKind.Mod => "%", BinaryKind.Pow => "^", BinaryKind.Concat => "..", BinaryKind.Eq => "==", BinaryKind.Ne => "~=",
		BinaryKind.Lt => "<", BinaryKind.Le => "<=", BinaryKind.Gt => ">", BinaryKind.Ge => ">=", BinaryKind.And => "and",
		BinaryKind.Or => "or", BinaryKind.BitAnd => "&", BinaryKind.BitOr => "|", BinaryKind.BitXor => "~", BinaryKind.Shl => "<<",
		_ => ">>"
	};
}
