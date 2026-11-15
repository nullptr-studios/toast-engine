using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Syntax;
using Loretta.CodeAnalysis;
using Loretta.CodeAnalysis.Lua;
using Loretta.CodeAnalysis.Lua.Syntax;
using Loretta.CodeAnalysis.Text;

namespace editor.LuaEditor.Graph;

internal sealed class GraphBuilder {
	private static readonly Dictionary<string, BinaryKind> s_binary = new() {
		["+"] = BinaryKind.Add, ["-"] = BinaryKind.Sub, ["*"] = BinaryKind.Mul, ["/"] = BinaryKind.Div, ["//"] = BinaryKind.FloorDiv,
		["%"] = BinaryKind.Mod, ["^"] = BinaryKind.Pow, [".."] = BinaryKind.Concat, ["=="] = BinaryKind.Eq, ["~="] = BinaryKind.Ne,
		["<"] = BinaryKind.Lt, ["<="] = BinaryKind.Le, [">"] = BinaryKind.Gt, [">="] = BinaryKind.Ge, ["and"] = BinaryKind.And,
		["or"] = BinaryKind.Or, ["&"] = BinaryKind.BitAnd, ["|"] = BinaryKind.BitOr, ["~"] = BinaryKind.BitXor, ["<<"] = BinaryKind.Shl,
		[">>"] = BinaryKind.Shr,
	};

	private static readonly Dictionary<string, UnaryKind> s_unary = new() {
		["-"] = UnaryKind.Neg, ["not"] = UnaryKind.Not, ["#"] = UnaryKind.Len, ["~"] = UnaryKind.BitNot,
	};

	private sealed class Scope(Scope? parent) {
		private readonly Dictionary<string, Binding> m_names = [];

		public Scope? Parent { get; } = parent;

		public void Declare(Binding binding) => m_names[binding.Name] = binding;

		public Binding? Find(string name) {
			for (var scope = this; scope is not null; scope = scope.Parent)
				if (scope.m_names.TryGetValue(name, out var found)) return found;
			return null;
		}
	}

	private readonly LuaGraph m_graph;
	private readonly LuaDocument m_doc;
	private readonly List<(InputPin Pin, Binding Binding, Statement? Statement)> m_refs = [];
	private readonly List<Binding> m_bindings = [];
	private Scope m_scope = new(null);
	private Statement? m_statement;

	public GraphBuilder(LuaGraph graph, LuaDocument doc) {
		m_graph = graph;
		m_doc = doc;
	}

	public void Build() {
		var module = m_graph.Module;
		module.OpenEnd = 0;
		module.CloseStart = m_doc.Text.Length;
		BuildBlock(module, m_doc.Root.Statements, m_doc.Root.EndOfFileToken);
		Finish();
		GraphSaver.Snapshot(m_graph);
	}

	public List<Statement> BuildFragment(IEnumerable<Binding> visibleNearestFirst) {
		foreach (var binding in visibleNearestFirst.Reverse()) m_scope.Declare(binding);
		var holder = new Block("fragment");
		BuildBlock(holder, m_doc.Root.Statements, m_doc.Root.EndOfFileToken);
		Finish();
		var statements = holder.Statements.ToList();
		foreach (var statement in statements) {
			holder.Remove(statement);
			Detach(statement);
		}
		return statements;
	}

	// pasted nodes must not point into clipboard source spans
	private static void Detach(Statement statement) {
		statement.Source = null;
		statement.Spans.Clear();
		statement.LeadingLinesSpan = null;
		statement.Baseline = null;
		switch (statement) {
			case CommentStatement comment:
				comment.LineSpan = null;
				break;
			case CastStatement cast:
				cast.LineSpan = null;
				break;
			case FunctionDecl function:
				function.Entry.ParamsSpan = null;
				break;
		}
		foreach (var pin in statement.Inputs) DetachPin(pin);
		foreach (var block in statement.ChildBlocks) DetachBlock(block);
	}

	private static void DetachBlock(Block block) {
		block.OpenEnd = -1;
		block.CloseStart = -1;
		block.Original = [];
		foreach (var statement in block.Statements) Detach(statement);
	}

	private static void DetachPin(InputPin pin) {
		pin.Origin = null;
		if (pin.Source is not { Binding: null } source || source.Owner is not ExprNode node) return;
		foreach (var input in node.Inputs) DetachPin(input);
		if (node is FunctionNode function) {
			function.Entry.ParamsSpan = null;
			DetachBlock(function.Entry.Body);
		}
	}

	private void Finish() {
		foreach (var binding in m_bindings) binding.IsMutable = binding.Assignments > 0;
		foreach (var (pin, binding, statement) in m_refs) {
			if (binding.IsMutable) {
				var get = New(new VariableGet(m_graph, binding), statement);
				pin.Connect(get.Result);
			} else {
				pin.Connect(binding.Output);
			}
		}
	}

	private T New<T>(T node, Statement? owner = null) where T : GraphNode {
		node.OwnerStatement = owner ?? m_statement;
		return node;
	}

	private void Declare(Binding binding) {
		m_scope.Declare(binding);
		m_bindings.Add(binding);
	}

	private void Push() => m_scope = new Scope(m_scope);

	private void Pop() => m_scope = m_scope.Parent!;

	private static TextSpan Bounds(IEnumerable<SyntaxNode> nodes) {
		var list = nodes.ToList();
		return TextSpan.FromBounds(list[0].Span.Start, list[^1].Span.End);
	}

	private void BuildBlock(Block block, StatementListSyntax list, SyntaxToken closeToken) {
		var carried = new List<TriviaLine>();
		foreach (var syntax in list.Statements) {
			var attached = EmitLeadingLines(block, TriviaScanner.LeadingLines(syntax.GetFirstToken()), true);
			var statement = BuildStatement(syntax);
			if (statement is null) {
				carried.AddRange(attached);
				continue;
			}
			if (carried.Count > 0) {
				attached = [.. carried, .. attached];
				carried.Clear();
			}
			if (attached.Count > 0) {
				statement.LeadingLines.AddRange(attached.Select(l => l.Text));
				statement.LeadingLinesSpan = TextSpan.FromBounds(attached[0].Span.Start, attached[^1].Span.End);
			}
			block.Add(statement);
		}
		EmitLeadingLines(block, TriviaScanner.LeadingLines(closeToken), false);
	}

	private List<TriviaLine> EmitLeadingLines(Block block, IReadOnlyList<TriviaLine> lines, bool attach) {
		var group = new List<TriviaLine>();
		void Flush() {
			if (group.Count == 0) return;
			var comment = new CommentStatement(m_graph, group.Select(l => l.Text)) { LineSpan = TextSpan.FromBounds(group[0].Span.Start, group[^1].Span.End) };
			block.Add(comment);
			group.Clear();
		}

		foreach (var line in lines) {
			switch (line.Kind) {
				case TriviaLineKind.Blank:
					Flush();
					break;
				case TriviaLineKind.Cast:
					Flush();
					var parts = line.Text["---@cast".Length..].Trim().Split(new[] { ' ', '\t' }, 2, StringSplitOptions.RemoveEmptyEntries);
					var cast = new CastStatement(m_graph, parts.ElementAtOrDefault(0) ?? "", parts.ElementAtOrDefault(1) ?? "") { LineSpan = line.Span, Target = m_scope.Find(parts.ElementAtOrDefault(0) ?? "") };
					block.Add(cast);
					break;
				default:
					group.Add(line);
					break;
			}
		}
		if (attach) return group;
		Flush();
		return [];
	}

	private Statement? BuildStatement(StatementSyntax syntax) {
		var saved = m_statement;
		try {
			var statement = BuildStatementCore(syntax);
			if (statement is not null) statement.Source = syntax;
			return statement;
		} finally {
			m_statement = saved;
		}
	}

	private T Begin<T>(T statement) where T : Statement {
		m_statement = statement;
		return statement;
	}

	private Statement? BuildStatementCore(StatementSyntax syntax) {
		switch (syntax) {
			case EmptyStatementSyntax:
				return null;
			case LocalVariableDeclarationStatementSyntax local:
				return BuildLocal(local);
			case LocalFunctionDeclarationStatementSyntax localFunction:
				return BuildLocalFunction(localFunction);
			case FunctionDeclarationStatementSyntax function:
				return BuildFunctionDeclaration(function);
			case AssignmentStatementSyntax assignment:
				return BuildAssignment(assignment);
			case ExpressionStatementSyntax { Expression: FunctionCallExpressionSyntax or MethodCallExpressionSyntax } expression: {
				var s = Begin(new CallStatement(m_graph));
				BindExpr(s.Call, expression.Expression);
				return s;
			}
			case IfStatementSyntax @if:
				return BuildIf(@if);
			case WhileStatementSyntax @while: {
				var s = Begin(new WhileStatement(m_graph));
				BindExpr(s.Condition, @while.Condition);
				Scoped(s.Body, @while.Body, @while.DoKeyword, @while.EndKeyword);
				return s;
			}
			case RepeatUntilStatementSyntax repeat: {
				var s = Begin(new RepeatStatement(m_graph));
				s.Body.OpenEnd = repeat.RepeatKeyword.Span.End;
				s.Body.CloseStart = repeat.UntilKeyword.Span.Start;
				// repeat conditions can read locals from the loop body
				Push();
				BuildBlock(s.Body, repeat.Body, repeat.UntilKeyword);
				BindExpr(s.Condition, repeat.Condition);
				Pop();
				return s;
			}
			case NumericForStatementSyntax numeric:
				return BuildNumericFor(numeric);
			case GenericForStatementSyntax generic:
				return BuildGenericFor(generic);
			case DoStatementSyntax @do: {
				var s = Begin(new DoStatement(m_graph));
				Scoped(s.Body, @do.Body, @do.DoKeyword, @do.EndKeyword);
				return s;
			}
			case ReturnStatementSyntax @return: {
				var s = Begin(new ReturnStatement(m_graph));
				foreach (var value in @return.Expressions) BindExpr(s.AddValue(), value);
				return s;
			}
			case BreakStatementSyntax:
				return Begin(new BreakStatement(m_graph));
			case GotoStatementSyntax @goto: {
				var s = Begin(new GotoStatement(m_graph, @goto.LabelName.Text));
				s.Spans["label"] = @goto.LabelName.Span;
				return s;
			}
			case GotoLabelStatementSyntax label: {
				var s = Begin(new LabelStatement(m_graph, label.Identifier.Text));
				s.Spans["name"] = label.Identifier.Span;
				return s;
			}
			default:
				m_graph.Warn($"line {m_doc.LineOf(syntax.Span.Start)}: {syntax.GetType().Name} is kept as text");
				return Begin(new RawStatement(m_graph, syntax.ToString()));
		}
	}

	private void Scoped(Block block, StatementListSyntax body, SyntaxToken open, SyntaxToken close, Action? declare = null) {
		block.OpenEnd = open.Span.End;
		block.CloseStart = close.Span.Start;
		Push();
		declare?.Invoke();
		BuildBlock(block, body, close);
		Pop();
	}

	private Statement BuildLocal(LocalVariableDeclarationStatementSyntax syntax) {
		var s = Begin(new LocalDecl(m_graph));
		if (syntax.EqualsValues is not null)
			foreach (var value in syntax.EqualsValues.Values) BindExpr(s.AddValue(), value);
		// a redeclared local still uses the outer binding in its initializer
		var created = new List<Binding>();
		foreach (var name in syntax.Names) {
			var binding = s.AddName(name.Name);
			if (!string.IsNullOrEmpty(name.AttributeName)) binding.Attribute = name.AttributeName;
			created.Add(binding);
		}
		s.Spans["names"] = Bounds(syntax.Names);
		foreach (var binding in created) Declare(binding);
		return s;
	}

	private Statement BuildLocalFunction(LocalFunctionDeclarationStatementSyntax syntax) {
		var s = Begin(new FunctionDecl(m_graph, true, syntax.Name.Name));
		Declare(s.FunctionBinding!);
		s.Spans["name"] = syntax.Name.Span;
		BuildFunctionBody(s.Entry, syntax.Parameters, syntax.Body, false, syntax.EndKeyword);
		return s;
	}

	private Statement BuildFunctionDeclaration(FunctionDeclarationStatementSyntax syntax) {
		var path = new List<string>();
		var isMethod = false;
		void Flatten(FunctionNameSyntax name) {
			switch (name) {
				case SimpleFunctionNameSyntax simple:
					path.Add(simple.Name.Text);
					break;
				case MemberFunctionNameSyntax member:
					Flatten(member.BaseName);
					path.Add(member.Name.Text);
					break;
				case MethodFunctionNameSyntax method:
					Flatten(method.BaseName);
					path.Add(method.Name.Text);
					isMethod = true;
					break;
			}
		}
		Flatten(syntax.Name);

		var s = Begin(new FunctionDecl(m_graph, false, path[0]));
		s.Path.Clear();
		s.Path.AddRange(path);
		s.IsMethod = isMethod;
		s.Spans["name"] = syntax.Name.Span;
		if (path.Count > 1) {
			ResolveName(s.Root, path[0]);
		} else if (m_scope.Find(path[0]) is { } existing) {
			s.AssignedBinding = existing;
			existing.Assignments++;
		}
		BuildFunctionBody(s.Entry, syntax.Parameters, syntax.Body, isMethod, syntax.EndKeyword);
		return s;
	}

	private void BuildFunctionBody(FunctionEntry entry, ParameterListSyntax parameters, StatementListSyntax body, bool isMethod, SyntaxToken end) {
		entry.ParamsSpan = parameters.Span;
		entry.Body.OpenEnd = parameters.CloseParenthesisToken.Span.End;
		entry.Body.CloseStart = end.Span.Start;
		var outer = m_statement;
		Push();
		if (isMethod) Declare(entry.AddParam("self", BindingKind.Self));
		foreach (var parameter in parameters.Parameters) {
			switch (parameter) {
				case NamedParameterSyntax named:
					Declare(entry.AddParam(named.Name, BindingKind.Param));
					break;
				case VarArgParameterSyntax:
					entry.IsVararg = true;
					break;
			}
		}
		BuildBlock(entry.Body, body, end);
		Pop();
		m_statement = outer;
	}

	private Statement BuildAssignment(AssignmentStatementSyntax syntax) {
		var s = Begin(new AssignStatement(m_graph));
		foreach (var variable in syntax.Variables) {
			switch (variable) {
				case IdentifierNameSyntax identifier:
					if (m_scope.Find(identifier.Name) is { } binding) s.AddBindingTarget(binding);
					else s.AddGlobalTarget(identifier.Name);
					break;
				case MemberAccessExpressionSyntax member: {
					var target = s.AddMemberTarget(member.MemberName.Text);
					BindExpr(target.Object!, member.Expression);
					break;
				}
				case ElementAccessExpressionSyntax element: {
					var target = s.AddIndexTarget();
					BindExpr(target.Object!, element.Expression);
					BindExpr(target.Key!, element.KeyExpression);
					break;
				}
				default:
					throw new InvalidOperationException($"cannot assign to {variable.GetType().Name}");
			}
		}
		s.Spans["targets"] = Bounds(syntax.Variables);
		foreach (var value in syntax.EqualsValues.Values) BindExpr(s.AddValue(), value);
		return s;
	}

	private Statement BuildIf(IfStatementSyntax syntax) {
		var s = Begin(new IfStatement(m_graph));
		var keywords = new List<SyntaxToken>();
		keywords.AddRange(syntax.ElseIfClauses.Select(c => c.ElseIfKeyword));
		if (syntax.ElseClause is not null) keywords.Add(syntax.ElseClause.ElseKeyword);
		keywords.Add(syntax.EndKeyword);

		var first = s.AddClause();
		BindExpr(first.Condition, syntax.Condition);
		Scoped(first.Body, syntax.Body, syntax.ThenKeyword, keywords[0]);
		var next = 1;
		foreach (var clause in syntax.ElseIfClauses) {
			var c = s.AddClause();
			BindExpr(c.Condition, clause.Condition);
			Scoped(c.Body, clause.Body, clause.ThenKeyword, keywords[next++]);
		}
		if (syntax.ElseClause is not null) Scoped(s.AddElse(), syntax.ElseClause.ElseBody, syntax.ElseClause.ElseKeyword, syntax.EndKeyword);
		return s;
	}

	private Statement BuildNumericFor(NumericForStatementSyntax syntax) {
		var s = Begin(new NumericFor(m_graph, syntax.Identifier.Name));
		s.Spans["var"] = syntax.Identifier.Span;
		BindExpr(s.Start, syntax.InitialValue);
		BindExpr(s.Limit, syntax.FinalValue);
		if (syntax.StepValue is not null) BindExpr(s.Step, syntax.StepValue);
		Scoped(s.Body, syntax.Body, syntax.DoKeyword, syntax.EndKeyword, () => Declare(s.Var));
		return s;
	}

	private Statement BuildGenericFor(GenericForStatementSyntax syntax) {
		var s = Begin(new GenericFor(m_graph));
		foreach (var expression in syntax.Expressions) BindExpr(s.AddExpr(), expression);
		var vars = syntax.Identifiers.Select(i => s.AddVar(i.Name)).ToList();
		s.Spans["vars"] = Bounds(syntax.Identifiers);
		Scoped(s.Body, syntax.Body, syntax.DoKeyword, syntax.EndKeyword, () => vars.ForEach(Declare));
		return s;
	}

	private void ResolveName(InputPin pin, string name) {
		if (m_scope.Find(name) is { } binding) {
			m_refs.Add((pin, binding, m_statement));
			return;
		}
		pin.Connect(New(new GlobalGet(m_graph, name)).Result);
	}

	private static bool IsMultiValue(ExpressionSyntax expression) {
		while (expression is ParenthesizedExpressionSyntax paren) expression = paren.Expression;
		return expression is FunctionCallExpressionSyntax or MethodCallExpressionSyntax or VarArgExpressionSyntax;
	}

	private void BindExpr(InputPin pin, ExpressionSyntax expression) {
		pin.Origin = expression;
		switch (expression) {
			case LiteralExpressionSyntax literal:
				BindLiteral(pin, literal);
				return;
			case VarArgExpressionSyntax:
				pin.Connect(New(new VarargNode(m_graph)).Result);
				return;
			case IdentifierNameSyntax identifier:
				ResolveName(pin, identifier.Name);
				return;
			case ParenthesizedExpressionSyntax paren:
				if (IsMultiValue(paren.Expression)) {
					var node = New(new ParenNode(m_graph));
					pin.Connect(node.Result);
					BindExpr(node.Inner, paren.Expression);
				} else {
					BindExpr(pin, paren.Expression);
				}
				pin.Origin = expression;
				return;
			case MemberAccessExpressionSyntax member: {
				var node = New(new MemberGet(m_graph, member.MemberName.Text));
				pin.Connect(node.Result);
				BindExpr(node.Object, member.Expression);
				return;
			}
			case ElementAccessExpressionSyntax element: {
				var node = New(new IndexGet(m_graph));
				pin.Connect(node.Result);
				BindExpr(node.Object, element.Expression);
				BindExpr(node.Key, element.KeyExpression);
				return;
			}
			case FunctionCallExpressionSyntax call: {
				var node = New(new CallNode(m_graph, CallStyle.Function, null));
				pin.Connect(node.Result);
				BindExpr(node.Target, call.Expression);
				BindArguments(node, call.Argument);
				return;
			}
			case MethodCallExpressionSyntax method: {
				var node = New(new CallNode(m_graph, CallStyle.Method, method.Identifier.Text));
				pin.Connect(node.Result);
				BindExpr(node.Target, method.Expression);
				BindArguments(node, method.Argument);
				return;
			}
			case AnonymousFunctionExpressionSyntax function: {
				var node = New(new FunctionNode(m_graph) { SourceSpan = function.Span });
				pin.Connect(node.Result);
				BuildFunctionBody(node.Entry, function.Parameters, function.Body, false, function.EndKeyword);
				return;
			}
			case TableConstructorExpressionSyntax table:
				BindTable(pin, table);
				return;
			case BinaryExpressionSyntax binary when s_binary.TryGetValue(binary.OperatorToken.Text, out var kind): {
				var node = New(new BinaryOp(m_graph, kind));
				pin.Connect(node.Result);
				BindExpr(node.Left, binary.Left);
				BindExpr(node.Right, binary.Right);
				return;
			}
			case UnaryExpressionSyntax unary when s_unary.TryGetValue(unary.OperatorToken.Text, out var kind): {
				var node = New(new UnaryOp(m_graph, kind));
				pin.Connect(node.Result);
				BindExpr(node.Operand, unary.Operand);
				return;
			}
			default:
				m_graph.Warn($"line {m_doc.LineOf(expression.Span.Start)}: {expression.GetType().Name} is kept as text");
				pin.Connect(New(new RawExpression(m_graph, expression.ToString())).Result);
				return;
		}
	}

	private void BindLiteral(InputPin pin, LiteralExpressionSyntax literal) {
		var text = literal.Token.Text;
		var kind = literal.Token.Kind().ToString();
		if (text == "nil") {
			pin.SetConstant(LuaConstant.Nil);
		} else if (text is "true" or "false") {
			pin.SetConstant(text == "true" ? LuaConstant.True : LuaConstant.False);
		} else if (kind.Contains("String")) {
			pin.SetConstant(new LuaConstant(ConstantKind.String, text));
		} else {
			pin.SetConstant(LuaConstant.Number(text));
		}
	}

	private void BindArguments(CallNode node, FunctionArgumentSyntax argument) {
		switch (argument) {
			case ExpressionListFunctionArgumentSyntax list:
				foreach (var expression in list.Expressions) BindExpr(node.AddArg(), expression);
				break;
			case StringFunctionArgumentSyntax text:
				BindExpr(node.AddArg(), text.Expression);
				break;
			case TableConstructorFunctionArgumentSyntax table:
				BindExpr(node.AddArg(), table.TableConstructor);
				break;
		}
	}

	private void BindTable(InputPin pin, TableConstructorExpressionSyntax table) {
		var node = New(new TableNode(m_graph));
		pin.Connect(node.Result);
		foreach (var field in table.Fields) {
			switch (field) {
				case UnkeyedTableFieldSyntax unkeyed:
					BindExpr(node.AddPositional().Value, unkeyed.Value);
					break;
				case IdentifierKeyedTableFieldSyntax named:
					BindExpr(node.AddNamed(named.Identifier.Text).Value, named.Value);
					break;
				case ExpressionKeyedTableFieldSyntax keyed: {
					var added = node.AddKeyed();
					BindExpr(added.Key!, keyed.Key);
					BindExpr(added.Value, keyed.Value);
					break;
				}
			}
		}
	}
}
