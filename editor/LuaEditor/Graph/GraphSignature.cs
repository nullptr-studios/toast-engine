using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace editor.LuaEditor.Graph;

internal static class GraphSignature {
	public static string Of(LuaGraph graph) {
		var writer = new Writer();
		writer.Block(graph.Module);
		return writer.ToString();
	}

	// external bindings need the same identity on both sides
	public static bool SameExpression(InputPin first, InputPin second) {
		var writer = new Writer(identifyBindings: true);
		return writer.Expression(first) == writer.Expression(second);
	}

	private sealed class Writer(bool identifyBindings = false) {
		private readonly StringBuilder m_sb = new();
		private readonly Dictionary<Binding, int> m_ids = [];

		public override string ToString() => m_sb.ToString();

		private void Line(string text) => m_sb.Append(text).Append('\n');

		private void Declare(Binding binding) => m_ids[binding] = m_ids.Count + 1;

		private string Name(Binding binding) {
			if (m_ids.TryGetValue(binding, out var id)) return "$" + id;
			if (!identifyBindings) return "?" + binding.Name;
			Declare(binding);
			return "$" + m_ids[binding];
		}

		public void Block(Block block) {
			Line("{");
			foreach (var statement in block.Statements) Statement(statement);
			Line("}");
		}

		private void Statement(Statement statement) {
			if (statement is SequenceStatement sequence) {
				foreach (var branch in sequence.Branches) foreach (var inner in branch.Statements) Statement(inner);
				return;
			}
			foreach (var line in statement.LeadingLines) Line("lead " + line);

			switch (statement) {
				case LocalDecl local: {
					var values = string.Join(", ", local.Values.Select(Expression));
					foreach (var name in local.Names) Declare(name);
					Line($"local {string.Join(", ", local.Names.Select(n => Name(n) + (n.Attribute is null ? "" : "<" + n.Attribute + ">")))} = {values}");
					break;
				}
				case AssignStatement assign: {
					var targets = string.Join(", ", assign.Targets.Select(t => t.Kind switch {
						TargetKind.Binding => Name(t.Binding!),
						TargetKind.Global => "@" + t.Name,
						TargetKind.Member => Expression(t.Object!) + "." + t.Name,
						_ => Expression(t.Object!) + "[" + Expression(t.Key!) + "]",
					}));
					Line($"assign {targets} = {string.Join(", ", assign.Values.Select(Expression))}");
					break;
				}
				case CallStatement call:
					Line("call " + Expression(call.Call));
					break;
				case IfStatement @if:
					for (var i = 0; i < @if.Clauses.Count; i++) {
						Line((i == 0 ? "if " : "elseif ") + Expression(@if.Clauses[i].Condition));
						Block(@if.Clauses[i].Body);
					}
					if (@if.Else is not null) {
						Line("else");
						Block(@if.Else);
					}
					break;
				case WhileStatement @while:
					Line("while " + Expression(@while.Condition));
					Block(@while.Body);
					break;
				case RepeatStatement repeat:
					Line("repeat");
					Line("{");
					foreach (var inner in repeat.Body.Statements) Statement(inner);
					Line("until " + Expression(repeat.Condition));
					Line("}");
					break;
				case NumericFor numeric:
					Line($"for {Expression(numeric.Start)}, {Expression(numeric.Limit)}, {(numeric.Step.IsEmpty ? "" : Expression(numeric.Step))}");
					Declare(numeric.Var);
					Line("var " + Name(numeric.Var));
					Block(numeric.Body);
					break;
				case GenericFor generic:
					Line("forin " + string.Join(", ", generic.Exprs.Select(Expression)));
					foreach (var variable in generic.Vars) Declare(variable);
					Line("vars " + string.Join(", ", generic.Vars.Select(Name)));
					Block(generic.Body);
					break;
				case DoStatement @do:
					Line("do");
					Block(@do.Body);
					break;
				case FunctionDecl function:
					FunctionDeclaration(function);
					break;
				case ReturnStatement @return:
					Line("return " + string.Join(", ", @return.Values.Select(Expression)));
					break;
				case BreakStatement:
					Line("break");
					break;
				case GotoStatement @goto:
					Line("goto " + @goto.Label);
					break;
				case LabelStatement label:
					Line("label " + label.Name);
					break;
				case CastStatement cast:
					Line($"cast {(cast.Target is null ? "@" + cast.Variable : Name(cast.Target))} {cast.TypeText}");
					break;
				case CommentStatement comment:
					Line("comment " + string.Join("|", comment.Lines));
					break;
				case RawStatement raw:
					Line("raw " + raw.Text);
					break;
			}
		}

		private void FunctionDeclaration(FunctionDecl function) {
			var target = function.Path.Count > 1
				? Expression(function.Root) + "." + string.Join(".", function.Path.Skip(1))
				: function.IsLocal ? "local" : (function.AssignedBinding is { } assigned ? Name(assigned) : "@" + function.Path[0]);
			if (function.FunctionBinding is { } own) Declare(own);
			Line($"function {target} method={function.IsMethod}");
			Entry(function.Entry);
		}

		private void Entry(FunctionEntry entry) {
			if (entry.Self is { } self) Declare(self);
			foreach (var parameter in entry.Params) Declare(parameter);
			Line($"params {entry.Self is not null} {entry.Params.Count} vararg={entry.IsVararg}");
			Block(entry.Body);
		}

		public string Expression(InputPin pin) {
			if (pin.Constant is { } constant) return constant.Raw;
			if (pin.Source is not { } source) return "nil";
			if (source.Binding is { } binding) return Name(binding);
			var text = Node(source.Owner);
			return source.Owner is CallNode && source.Index > 0 ? $"@select({source.Index + 1}, {text})" : text;
		}

		private string Node(GraphNode node) {
			switch (node) {
				case GlobalGet global:
					return "@" + global.Name;
				case VariableGet get:
					return Name(get.Binding);
				case VarargNode:
					return "...";
				case RawExpression raw:
					return "raw(" + raw.Text + ")";
				case ParenNode paren:
					// only calls and varargs change arity when parenthesized
					return paren.Inner.Source is { Binding: null, Index: 0, Owner: CallNode or VarargNode }
						? "paren(" + Expression(paren.Inner) + ")"
						: Expression(paren.Inner);
				case MemberGet member:
					return Expression(member.Object) + "." + member.Name;
				case IndexGet index:
					return Expression(index.Object) + "[" + Expression(index.Key) + "]";
				case CallNode call:
					return call.Style == CallStyle.Method
						? $"{Expression(call.Target)}:{call.Method}({string.Join(", ", call.Args.Select(Expression))})"
						: $"{Expression(call.Target)}({string.Join(", ", call.Args.Select(Expression))})";
				case BinaryOp binary:
					return $"({Expression(binary.Left)} {binary.Op} {Expression(binary.Right)})";
				case UnaryOp unary:
					// parsing turns negative constants into unary expressions
					if (unary.Op == UnaryKind.Neg && unary.Operand.Constant is { Kind: ConstantKind.Number } number && !number.Raw.StartsWith('-')) return "-" + number.Raw;
					return $"({unary.Op} {Expression(unary.Operand)})";
				case TableNode table:
					return "{" + string.Join(", ", table.Fields.Select(f => f.Kind switch {
						TableFieldKind.Positional => Expression(f.Value),
						TableFieldKind.Named => f.Name + "=" + Expression(f.Value),
						_ => "[" + Expression(f.Key!) + "]=" + Expression(f.Value),
					})) + "}";
				case FunctionNode function: {
					var inner = new Writer { };
					foreach (var (b, id) in m_ids) inner.m_ids[b] = id;
					inner.Entry(function.Entry);
					return "function<" + inner.m_sb.ToString().Replace("\n", ";") + ">";
				}
				default:
					return node.GetType().Name;
			}
		}
	}
}
