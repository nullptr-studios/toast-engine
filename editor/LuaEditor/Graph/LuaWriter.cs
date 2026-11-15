using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using editor.LuaEditor.Syntax;

namespace editor.LuaEditor.Graph;

public sealed class LuaWriter {
	private static readonly HashSet<string> s_keywords = [
		"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local", "nil", "not", "or",
		"repeat", "return", "then", "true", "until", "while",
	];

	private const int UnaryPrecedence = 12;
	private const int PowerPrecedence = 14;
	private const int AtomPrecedence = 100;

	private readonly string m_eol;
	private readonly string m_unit;

	internal Func<FunctionNode, int, string?>? VerbatimClosure { get; set; }

	internal Func<Statement, int, string?>? VerbatimStatement { get; set; }

	public LuaWriter(LuaDocument style) : this(style.Eol, style.IndentUnit) { }

	public LuaWriter(string eol, string indentUnit) {
		m_eol = eol;
		m_unit = indentUnit;
	}

	public static string File(LuaGraph graph) {
		var writer = new LuaWriter(graph.Document);
		var text = writer.Block(graph.Module, 0);
		return text.Length == 0 ? "" : text + graph.Document.Eol;
	}

	public static bool IsIdentifier(string name) {
		if (name.Length == 0 || s_keywords.Contains(name) || !(char.IsAsciiLetter(name[0]) || name[0] == '_')) return false;
		return name.All(c => char.IsAsciiLetterOrDigit(c) || c == '_');
	}

	private string Ind(int level) => level <= 0 ? "" : string.Concat(Enumerable.Repeat(m_unit, level));

	public string Block(Block block, int indent) => Statements(block.Statements, indent);

	public string Statements(IReadOnlyList<Statement> statements, int indent) {
		var sb = new StringBuilder();
		for (var i = 0; i < statements.Count; i++) {
			var statement = statements[i];
			if (VerbatimStatement?.Invoke(statement, indent) is { } verbatim) {
				if (sb.Length > 0) sb.Append(m_eol);
				sb.Append(verbatim);
				continue;
			}
			var text = Statement(statement, indent);
			if (text.Length == 0 && statement.LeadingLines.Count == 0) continue;
			if (sb.Length > 0) sb.Append(m_eol);
			foreach (var line in statement.LeadingLines) sb.Append(Ind(indent)).Append(line).Append(m_eol);
			if (text.Length > 0) sb.Append(Ind(indent)).Append(text);
			else if (statement.LeadingLines.Count > 0) sb.Length -= m_eol.Length;
			// a blank line keeps this comment separate from the next statement
			if (statement is CommentStatement && i < statements.Count - 1) sb.Append(m_eol);
		}
		return sb.ToString();
	}

	public string Statement(Statement statement, int indent) {
		switch (statement) {
			case LocalDecl local: {
				var names = string.Join(", ", local.Names.Select(n => n.Attribute is { Length: > 0 } a ? $"{n.Name} <{a}>" : n.Name));
				var text = "local " + names;
				if (local.Values.Count > 0) text += " = " + ExpressionList(local.Values, indent, false);
				return text;
			}
			case AssignStatement assign: {
				var text = Targets(assign, indent, false) + " = " + ExpressionList(assign.Values, indent, false);
				return text.StartsWith('(') ? ";" + text : text;
			}
			case CallStatement call: {
				var text = Expr(call.Call, indent, false, 0);
				return text.StartsWith('(') ? ";" + text : text;
			}
			case IfStatement @if:
				return WriteIf(@if, indent);
			case WhileStatement @while:
				return $"while {Expr(@while.Condition, indent, false, 0)} do" + Body(@while.Body, indent) + "end";
			case RepeatStatement repeat:
				return "repeat" + Body(repeat.Body, indent) + $"until {Expr(repeat.Condition, indent, false, 0)}";
			case NumericFor numeric:
				return NumericForHeader(numeric, indent, false) + Body(numeric.Body, indent) + "end";
			case GenericFor generic:
				return $"{GenericForHeader(generic, indent, false)}" + Body(generic.Body, indent) + "end";
			case DoStatement @do:
				return "do" + Body(@do.Body, indent) + "end";
			case FunctionDecl function:
				return FunctionHeader(function) + Body(function.Entry.Body, indent) + "end";
			case ReturnStatement @return:
				return @return.Values.Count == 0 ? "return" : "return " + ExpressionList(@return.Values, indent, false);
			case BreakStatement:
				return "break";
			case GotoStatement @goto:
				return "goto " + @goto.Label;
			case LabelStatement label:
				return $"::{label.Name}::";
			case CastStatement cast:
				return $"---@cast {cast.Variable} {cast.TypeText}".TrimEnd();
			case CommentStatement comment:
				return string.Join(m_eol + Ind(indent), comment.Lines);
			case SequenceStatement sequence: {
				var parts = sequence.Branches.Select(b => Block(b, indent)).Where(t => t.Length > 0).ToList();
				var joined = string.Join(m_eol, parts);
				var prefix = Ind(indent);
				return prefix.Length > 0 && joined.StartsWith(prefix, StringComparison.Ordinal) ? joined[prefix.Length..] : joined;
			}
			case RawStatement raw:
				return raw.Text;
			default:
				throw new InvalidOperationException($"no Lua form for {statement.GetType().Name}");
		}
	}

	internal string NamesText(LocalDecl local)
		=> string.Join(", ", local.Names.Select(n => n.Attribute is { Length: > 0 } a ? $"{n.Name} <{a}>" : n.Name));

	internal string Targets(AssignStatement assign, int indent, bool placeholder)
		=> string.Join(", ", assign.Targets.Select(t => t.Kind switch {
			TargetKind.Binding => t.Binding!.Name,
			TargetKind.Global => t.Name!,
			TargetKind.Member => MemberText(Prefix(t.Object!, indent, placeholder), t.Name!),
			_ => Prefix(t.Object!, indent, placeholder) + "[" + Expr(t.Key!, indent, placeholder, 0) + "]",
		}));

	internal string ForVarsText(GenericFor generic) => string.Join(", ", generic.Vars.Select(v => v.Name));

	internal string FunctionNameText(FunctionDecl function) {
		if (function.IsLocal) return function.FunctionBinding!.Name;
		var root = function.Path.Count > 1 ? NameOf(function.Root) : function.Path[0];
		if (function.Path.Count == 1) return function.AssignedBinding?.Name ?? function.Path[0];
		var sb = new StringBuilder(root);
		for (var i = 1; i < function.Path.Count; i++) sb.Append(function.IsMethod && i == function.Path.Count - 1 ? ':' : '.').Append(function.Path[i]);
		return sb.ToString();
	}

	internal string ParamsText(FunctionEntry entry) {
		var names = entry.Params.Select(p => p.Name).ToList();
		if (entry.IsVararg) names.Add("...");
		return "(" + string.Join(", ", names) + ")";
	}

	private string FunctionHeader(FunctionDecl function)
		=> (function.IsLocal ? "local function " : "function ") + FunctionNameText(function) + ParamsText(function.Entry);

	internal string NumericForHeader(NumericFor numeric, int indent, bool placeholder) {
		var text = $"for {numeric.Var.Name} = {Expr(numeric.Start, indent, placeholder, 0)}, {Expr(numeric.Limit, indent, placeholder, 0)}";
		if (!numeric.Step.IsEmpty) text += ", " + Expr(numeric.Step, indent, placeholder, 0);
		return text + " do";
	}

	internal string GenericForHeader(GenericFor generic, int indent, bool placeholder)
		=> $"for {ForVarsText(generic)} in {ExpressionList(generic.Exprs, indent, placeholder)} do";

	private string WriteIf(IfStatement statement, int indent) {
		var sb = new StringBuilder();
		for (var i = 0; i < statement.Clauses.Count; i++) {
			var clause = statement.Clauses[i];
			sb.Append(i == 0 ? "if " : Ind(indent) + "elseif ").Append(Expr(clause.Condition, indent, false, 0)).Append(" then");
			sb.Append(Body(clause.Body, indent, endsLine: false));
		}
		if (statement.Else is not null) sb.Append(Ind(indent)).Append("else").Append(Body(statement.Else, indent, endsLine: false));
		return sb.Append(Ind(indent)).Append("end").ToString();
	}

	private string Body(Block body, int indent, bool endsLine = true) {
		var text = Block(body, indent + 1);
		return m_eol + (text.Length > 0 ? text + m_eol : "") + (endsLine ? Ind(indent) : "");
	}

	public string Expression(InputPin pin) => Expr(pin, 0, false, 0);

	public string Expression(InputPin pin, bool bodyPlaceholder) => Expr(pin, 0, bodyPlaceholder, 0);

	internal string ExpressionAt(InputPin pin, int indent, bool placeholder) => Expr(pin, indent, placeholder, 0);

	internal string StatementWithLeading(Statement statement, int indent) {
		var text = new StringBuilder();
		foreach (var line in statement.LeadingLines) text.Append(Ind(indent)).Append(line).Append(m_eol);
		var body = Statement(statement, indent);
		if (body.Length > 0) text.Append(Ind(indent)).Append(body);
		else if (text.Length > 0) text.Length -= m_eol.Length;
		return text.ToString();
	}

	internal string ExpressionList(IEnumerable<InputPin> pins, int indent, bool placeholder)
		=> string.Join(", ", pins.Select(p => Expr(p, indent, placeholder, 0)));

	private static int PrecedenceOf(BinaryKind op) => op switch {
		BinaryKind.Or => 1,
		BinaryKind.And => 2,
		BinaryKind.Lt or BinaryKind.Gt or BinaryKind.Le or BinaryKind.Ge or BinaryKind.Ne or BinaryKind.Eq => 3,
		BinaryKind.BitOr => 4,
		BinaryKind.BitXor => 5,
		BinaryKind.BitAnd => 6,
		BinaryKind.Shl or BinaryKind.Shr => 7,
		BinaryKind.Concat => 9,
		BinaryKind.Add or BinaryKind.Sub => 10,
		BinaryKind.Mul or BinaryKind.Div or BinaryKind.FloorDiv or BinaryKind.Mod => 11,
		_ => PowerPrecedence,
	};

	// preserve the concat grouping produced by the parser
	private static bool RightAssociative(BinaryKind op) => op == BinaryKind.Pow;

	private static string OperatorText(BinaryKind op) => op switch {
		BinaryKind.Add => "+", BinaryKind.Sub => "-", BinaryKind.Mul => "*", BinaryKind.Div => "/", BinaryKind.FloorDiv => "//",
		BinaryKind.Mod => "%", BinaryKind.Pow => "^", BinaryKind.Concat => "..", BinaryKind.Eq => "==", BinaryKind.Ne => "~=",
		BinaryKind.Lt => "<", BinaryKind.Le => "<=", BinaryKind.Gt => ">", BinaryKind.Ge => ">=", BinaryKind.And => "and",
		BinaryKind.Or => "or", BinaryKind.BitAnd => "&", BinaryKind.BitOr => "|", BinaryKind.BitXor => "~", BinaryKind.Shl => "<<",
		_ => ">>",
	};

	// negative constants need grouping before exponentiation
	private static int Precedence(InputPin pin) => pin.Constant is { Kind: ConstantKind.Number, Raw: ['-', ..] } ? UnaryPrecedence : pin.Source?.Owner switch {
		BinaryOp binary when pin.Source.Binding is null => PrecedenceOf(binary.Op),
		UnaryOp when pin.Source.Binding is null => UnaryPrecedence,
		_ => AtomPrecedence,
	};

	private string Expr(InputPin pin, int indent, bool placeholder, int minPrecedence) {
		var text = Raw(pin, indent, placeholder);
		return Precedence(pin) < minPrecedence ? "(" + text + ")" : text;
	}

	private string Prefix(InputPin pin, int indent, bool placeholder) {
		var text = Raw(pin, indent, placeholder);
		return IsPrefixForm(pin) ? text : "(" + text + ")";
	}

	private static bool IsPrefixForm(InputPin pin) {
		if (pin.Source is not { } source) return false;
		if (source.Binding is not null) return true;
		return source.Owner is GlobalGet or VariableGet or MemberGet or IndexGet or ParenNode || (source.Owner is CallNode && source.Index == 0);
	}

	private string NameOf(InputPin pin) => pin.Source switch {
		{ Binding: { } binding } => binding.Name,
		{ Owner: GlobalGet global } => global.Name,
		{ Owner: VariableGet get } => get.Binding.Name,
		_ => Raw(pin, 0, false),
	};

	private string Raw(InputPin pin, int indent, bool placeholder) {
		if (pin.Constant is { } constant) return constant.Raw;
		if (pin.Source is not { } source) return "nil";
		if (source.Binding is { } binding) return binding.Name;

		var text = Node(source.Owner, indent, placeholder);
		return source.Owner is CallNode && source.Index > 0 ? $"select({source.Index + 1}, {text})" : text;
	}

	private string Node(GraphNode node, int indent, bool placeholder) {
		switch (node) {
			case GlobalGet global:
				return global.Name;
			case VariableGet get:
				return get.Binding.Name;
			case VarargNode:
				return "...";
			case RawExpression raw:
				return raw.Text;
			case ParenNode paren:
				return paren.Inner.IsEmpty || paren.Inner.Constant is not null || paren.Inner.Source is { Binding: not null }
					? Expr(paren.Inner, indent, placeholder, 0)
					: "(" + Expr(paren.Inner, indent, placeholder, 0) + ")";
			case MemberGet member:
				return MemberText(Prefix(member.Object, indent, placeholder), member.Name);
			case IndexGet index:
				return Prefix(index.Object, indent, placeholder) + "[" + Expr(index.Key, indent, placeholder, 0) + "]";
			case CallNode call: {
				var args = ExpressionList(call.Args, indent, placeholder);
				return call.Style == CallStyle.Method
					? $"{Prefix(call.Target, indent, placeholder)}:{call.Method}({args})"
					: $"{Prefix(call.Target, indent, placeholder)}({args})";
			}
			case BinaryOp binary: {
				var p = PrecedenceOf(binary.Op);
				var right = RightAssociative(binary.Op);
				// exponentiation accepts a unary expression on the right
				var rightMinimum = binary.Op == BinaryKind.Pow ? UnaryPrecedence : (right ? p : p + 1);
				return $"{Expr(binary.Left, indent, placeholder, right ? p + 1 : p)} {OperatorText(binary.Op)} {Expr(binary.Right, indent, placeholder, rightMinimum)}";
			}
			case UnaryOp unary: {
				var operand = Expr(unary.Operand, indent, placeholder, UnaryPrecedence);
				var op = unary.Op switch { UnaryKind.Neg => "-", UnaryKind.Not => "not ", UnaryKind.Len => "#", _ => "~" };
				// adjacent minus signs would start a lua comment
				return op + (operand.StartsWith('-') || operand.StartsWith('~') ? " " : "") + operand;
			}
			case TableNode table:
				return "{" + string.Join(", ", table.Fields.Select(f => f.Kind switch {
					TableFieldKind.Positional => Expr(f.Value, indent, placeholder, 0),
					TableFieldKind.Named => $"{f.Name} = {Expr(f.Value, indent, placeholder, 0)}",
					_ => $"[{Expr(f.Key!, indent, placeholder, 0)}] = {Expr(f.Value, indent, placeholder, 0)}",
				})) + "}";
			case FunctionNode function: {
				if (!placeholder && VerbatimClosure?.Invoke(function, indent) is { } verbatim) return verbatim;
				var header = "function" + ParamsText(function.Entry);
				if (placeholder) return header + " <body> end";
				var body = Block(function.Entry.Body, indent + 1);
				return header + (body.Length == 0 ? " end" : m_eol + body + m_eol + Ind(indent) + "end");
			}
			default:
				throw new InvalidOperationException($"no Lua form for {node.GetType().Name}");
		}
	}

	private static string MemberText(string prefix, string name)
		=> IsIdentifier(name) ? prefix + "." + name : prefix + "[\"" + name.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"]";
}
