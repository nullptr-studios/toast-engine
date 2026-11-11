using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using editor.LuaEditor.Types;
using Loretta.CodeAnalysis;
using Loretta.CodeAnalysis.Text;
using DiagnosticSeverity = editor.LuaEditor.Types.DiagnosticSeverity;

namespace editor.LuaEditor.Graph;

public enum ConstantKind { Nil, Boolean, Number, String }

public sealed record LuaConstant(ConstantKind Kind, string Raw) {
	public static readonly LuaConstant Nil = new(ConstantKind.Nil, "nil");
	public static readonly LuaConstant True = new(ConstantKind.Boolean, "true");
	public static readonly LuaConstant False = new(ConstantKind.Boolean, "false");

	public static LuaConstant Number(string raw) => new(ConstantKind.Number, raw);

	public static LuaConstant Integer(long value) => new(ConstantKind.Number, value.ToString(CultureInfo.InvariantCulture));

	public static LuaConstant Float(double value) {
		// lua has no literals for infinity or nan
		if (double.IsNaN(value)) return new LuaConstant(ConstantKind.Number, "(0/0)");
		if (double.IsPositiveInfinity(value)) return new LuaConstant(ConstantKind.Number, "math.huge");
		if (double.IsNegativeInfinity(value)) return new LuaConstant(ConstantKind.Number, "(-math.huge)");
		var text = value.ToString("R", CultureInfo.InvariantCulture);
		return new LuaConstant(ConstantKind.Number, text.Contains('.') || text.Contains('E') ? text : text + ".0");
	}

	public static LuaConstant String(string value) {
		var sb = new StringBuilder("\"");
		foreach (var c in value) {
			sb.Append(c switch {
				'"' => "\\\"",
				'\\' => "\\\\",
				'\n' => "\\n",
				'\r' => "\\r",
				'\t' => "\\t",
				// fixed width escapes keep the next digit out of the escape
				'\0' => "\\000",
				_ => c.ToString(),
			});
		}
		return new LuaConstant(ConstantKind.String, sb.Append('"').ToString());
	}

	public LuaType ValueType => Kind switch {
		ConstantKind.Nil => LuaType.Nil,
		ConstantKind.Boolean => LuaType.Boolean,
		ConstantKind.String => LuaType.String,
		_ => IsIntegerLiteral(Raw) ? LuaType.Integer : LuaType.Number,
	};

	private static bool IsIntegerLiteral(string raw) {
		if (raw.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) return !raw.Contains('.') && !raw.Contains('p') && !raw.Contains('P');
		return !raw.Contains('.') && !raw.Contains('e') && !raw.Contains('E');
	}
}

public enum CallStyle { Function, Method }

public enum BinaryKind { Add, Sub, Mul, Div, FloorDiv, Mod, Pow, Concat, Eq, Ne, Lt, Le, Gt, Ge, And, Or, BitAnd, BitOr, BitXor, Shl, Shr }

public enum UnaryKind { Neg, Not, Len, BitNot }

public enum BindingKind { Local, Param, LoopVar, LocalFunction, Self }

public enum TableFieldKind { Positional, Named, Keyed }

public enum TargetKind { Binding, Global, Member, Index }

public sealed class Binding {
	internal Binding(string name, BindingKind kind, GraphNode declarer, int outputIndex) {
		m_name = name;
		Kind = kind;
		Declarer = declarer;
		OutputIndex = outputIndex;
		declarer.Outputs[outputIndex].Binding = this;
	}

	private string m_name;

	public string Name {
		get => m_name;
		set {
			m_name = value;
			Output.Name = value;
		}
	}
	public BindingKind Kind { get; internal set; }
	public GraphNode Declarer { get; internal set; }
	public int OutputIndex { get; internal set; }

	public string? Attribute { get; set; }

	public bool IsMutable { get; internal set; }

	public int Assignments { get; internal set; }
	public bool IsDeclared { get; internal set; }

	public LuaType Type { get; internal set; } = LuaType.Unknown;
	public OutputPin Output => Declarer.Outputs[OutputIndex];
}

public sealed class OutputPin {
	private readonly List<InputPin> m_consumers = [];

	internal OutputPin(GraphNode owner, int index, string name) {
		Owner = owner;
		Index = index;
		Name = name;
	}

	public GraphNode Owner { get; }
	public int Index { get; internal set; }
	public string Name { get; set; }
	public LuaType Type { get; internal set; } = LuaType.Unknown;

	public Binding? Binding { get; internal set; }

	public IReadOnlyList<InputPin> Consumers => m_consumers;

	internal void AddConsumer(InputPin pin) => m_consumers.Add(pin);

	internal void RemoveConsumer(InputPin pin) => m_consumers.Remove(pin);
}

public sealed class InputPin {
	internal InputPin(GraphNode owner, string name) {
		Owner = owner;
		Name = name;
	}

	public GraphNode Owner { get; }
	public string Name { get; set; }
	public OutputPin? Source { get; private set; }
	public LuaConstant? Constant { get; private set; }
	public bool IsEmpty => Source is null && Constant is null;

	public LuaType? Expected { get; internal set; }

	public LuaType ActualType => ActualOverride ?? Source?.Type ?? Constant?.ValueType ?? LuaType.Nil;

	internal LuaType? ActualOverride { get; set; }

	internal SyntaxNode? Origin { get; set; }

	public void Connect(OutputPin source) {
		Clear();
		Source = source;
		source.AddConsumer(this);
	}

	public void SetConstant(LuaConstant constant) {
		Clear();
		Constant = constant;
	}

	public void Clear() {
		Source?.RemoveConsumer(this);
		Source = null;
		Constant = null;
	}
}

public abstract class GraphNode {
	private readonly List<InputPin> m_inputs = [];
	private readonly List<OutputPin> m_outputs = [];

	protected GraphNode(LuaGraph graph) {
		Graph = graph;
		Id = graph.Register(this);
	}

	public LuaGraph Graph { get; }
	public int Id { get; }
	public IReadOnlyList<InputPin> Inputs => m_inputs;
	public IReadOnlyList<OutputPin> Outputs => m_outputs;

	public bool IsExpanded { get; set; }

	public Statement? OwnerStatement { get; internal set; }

	protected InputPin AddInput(string name) {
		var pin = new InputPin(this, name);
		m_inputs.Add(pin);
		return pin;
	}

	protected OutputPin AddOutput(string name) {
		var pin = new OutputPin(this, m_outputs.Count, name);
		m_outputs.Add(pin);
		return pin;
	}

	internal void SetInputOrder(IEnumerable<InputPin> order) {
		var list = order.ToList();
		m_inputs.Clear();
		m_inputs.AddRange(list);
	}

	internal void RemoveInput(InputPin pin) {
		pin.Clear();
		m_inputs.Remove(pin);
	}

	internal void RemoveOutputAt(int index) {
		m_outputs.RemoveAt(index);
		for (var i = index; i < m_outputs.Count; i++) {
			m_outputs[i].Index = i;
			if (m_outputs[i].Binding is { } binding) binding.OutputIndex = i;
		}
	}

	internal InputPin NewInput(string name) => AddInput(name);

	internal OutputPin NewOutput(string name) => AddOutput(name);
}

public abstract class ExprNode : GraphNode {
	protected ExprNode(LuaGraph graph, string resultName) : base(graph) {
		AddOutput(resultName);
	}

	public OutputPin Result => Outputs[0];
}

public sealed class GlobalGet : ExprNode {
	public GlobalGet(LuaGraph graph, string name) : base(graph, name) {
		Name = name;
	}

	public string Name { get; set; }
}

public sealed class VariableGet : ExprNode {
	public VariableGet(LuaGraph graph, Binding binding) : base(graph, binding.Name) {
		Binding = binding;
	}

	public Binding Binding { get; }
}

public sealed class VarargNode : ExprNode {
	public VarargNode(LuaGraph graph) : base(graph, "...") { }
}

public sealed class MemberGet : ExprNode {
	public MemberGet(LuaGraph graph, string name) : base(graph, name) {
		Name = name;
		Object = AddInput("object");
	}

	public InputPin Object { get; }
	public string Name { get; set; }
}

public sealed class IndexGet : ExprNode {
	public IndexGet(LuaGraph graph) : base(graph, "value") {
		Object = AddInput("object");
		Key = AddInput("key");
	}

	public InputPin Object { get; }
	public InputPin Key { get; }
}

public sealed class CallNode : ExprNode {
	private readonly List<InputPin> m_args = [];

	public CallNode(LuaGraph graph, CallStyle style, string? method) : base(graph, "result") {
		Style = style;
		Method = method;
		Target = AddInput(style == CallStyle.Method ? "object" : "function");
	}

	public CallStyle Style { get; internal set; }

	public string? Method { get; set; }

	public InputPin Target { get; }

	public IReadOnlyList<InputPin> Args => m_args;
	public int ReturnCount { get; internal set; }

	public InputPin AddArg(string? name = null) {
		var pin = AddInput(name ?? "arg" + (m_args.Count + 1));
		m_args.Add(pin);
		return pin;
	}

	public InputPin InsertArg(int index, string name) {
		var pin = AddInput(name);
		m_args.Insert(index, pin);
		SetInputOrder(new[] { Target }.Concat(m_args));
		return pin;
	}

	internal void RemoveArgAt(int index) {
		var pin = m_args[index];
		m_args.RemoveAt(index);
		RemoveInput(pin);
	}

	internal void EnsureOutputs(int count) {
		while (Outputs.Count < count) AddOutput("result" + (Outputs.Count + 1));
	}
}

public sealed class BinaryOp : ExprNode {
	public BinaryOp(LuaGraph graph, BinaryKind op) : base(graph, "result") {
		Op = op;
		Left = AddInput("a");
		Right = AddInput("b");
	}

	public BinaryKind Op { get; set; }
	public InputPin Left { get; }
	public InputPin Right { get; }
}

public sealed class UnaryOp : ExprNode {
	public UnaryOp(LuaGraph graph, UnaryKind op) : base(graph, "result") {
		Op = op;
		Operand = AddInput("value");
	}

	public UnaryKind Op { get; set; }
	public InputPin Operand { get; }
}

public sealed class ParenNode : ExprNode {
	public ParenNode(LuaGraph graph) : base(graph, "value") {
		Inner = AddInput("value");
	}

	public InputPin Inner { get; }
}

public sealed class TableField {
	internal TableField(TableFieldKind kind, string? name, InputPin? key, InputPin value) {
		Kind = kind;
		Name = name;
		Key = key;
		Value = value;
	}

	public TableFieldKind Kind { get; }
	public string? Name { get; set; }
	public InputPin? Key { get; }
	public InputPin Value { get; }
}

public enum TableFlavor { Table, Array, Dictionary }

public sealed class TableNode : ExprNode {
	private readonly List<TableField> m_fields = [];

	public TableNode(LuaGraph graph) : base(graph, "table") { }

	public TableFlavor? DeclaredFlavor { get; set; }

	public LuaType? DeclaredType { get; set; }

	public LuaType? TargetType { get; internal set; }

	public TableFlavor Flavor => DeclaredFlavor
		?? TargetType switch {
			ArrayType => (TableFlavor?)TableFlavor.Array,
			TableType => TableFlavor.Dictionary,
			_ => null
		}
		?? (m_fields.Count > 0 && m_fields.All(f => f.Kind == TableFieldKind.Positional) ? TableFlavor.Array
			: m_fields.Count > 0 && m_fields.All(f => f.Kind == TableFieldKind.Keyed) ? TableFlavor.Dictionary
			: TableFlavor.Table);

	public IReadOnlyList<TableField> Fields => m_fields;

	public TableField AddPositional() {
		var field = new TableField(TableFieldKind.Positional, null, null, AddInput("[" + (m_fields.Count + 1) + "]"));
		m_fields.Add(field);
		return field;
	}

	public TableField AddNamed(string name) {
		var field = new TableField(TableFieldKind.Named, name, null, AddInput(name));
		m_fields.Add(field);
		return field;
	}

	public TableField AddKeyed() {
		var key = AddInput("key");
		var field = new TableField(TableFieldKind.Keyed, null, key, AddInput("value"));
		m_fields.Add(field);
		return field;
	}

	internal void RemoveFieldAt(int index) {
		var field = m_fields[index];
		m_fields.RemoveAt(index);
		if (field.Key is not null) RemoveInput(field.Key);
		RemoveInput(field.Value);
	}
}

public sealed class FunctionNode : ExprNode {
	public FunctionNode(LuaGraph graph) : base(graph, "function") {
		Entry = new FunctionEntry(graph, this);
	}

	public FunctionEntry Entry { get; }

	internal TextSpan? SourceSpan { get; set; }

	internal string? OriginalParams { get; set; }
}

public sealed class RawExpression : ExprNode {
	public RawExpression(LuaGraph graph, string text) : base(graph, "raw") {
		Text = text;
	}

	public string Text { get; set; }
}

public sealed class FunctionEntry : GraphNode {
	public FunctionEntry(LuaGraph graph, GraphNode owner) : base(graph) {
		Owner = owner;
		Body = new Block("body") { Owner = this };
	}

	public GraphNode Owner { get; }
	public Binding? Self { get; internal set; }
	public List<Binding> Params { get; } = [];
	public bool IsVararg { get; set; }

	internal TextSpan? ParamsSpan { get; set; }

	public Block Body { get; }

	internal void RemoveParam(Binding binding) {
		Params.Remove(binding);
		RemoveOutputAt(binding.OutputIndex);
	}

	internal Binding AddParam(string name, BindingKind kind) {
		var pin = AddOutput(name);
		var binding = new Binding(name, kind, this, pin.Index);
		if (kind == BindingKind.Self) Self = binding;
		else Params.Add(binding);
		return binding;
	}
}

public sealed class Block {
	private readonly List<Statement> m_statements = [];

	public Block(string role) {
		Role = role;
	}

	public string Role { get; }
	public GraphNode? Owner { get; internal set; }
	public IReadOnlyList<Statement> Statements => m_statements;

	internal int OpenEnd { get; set; } = -1;

	internal int CloseStart { get; set; } = -1;

	public void Insert(int index, Statement statement) {
		statement.Parent?.Remove(statement);
		m_statements.Insert(index, statement);
		statement.Parent = this;
	}

	public void Add(Statement statement) => Insert(m_statements.Count, statement);

	public bool Remove(Statement statement) {
		if (!m_statements.Remove(statement)) return false;
		statement.Parent = null;
		return true;
	}

	public int IndexOf(Statement statement) => m_statements.IndexOf(statement);

	internal IReadOnlyList<Statement> Original { get; set; } = [];
}

public abstract class Statement : GraphNode {
	protected Statement(LuaGraph graph) : base(graph) {
		OwnerStatement = this;
	}

	public Block? Parent { get; internal set; }

	public bool IsFloating { get; set; }

	public List<string> LeadingLines { get; } = [];

	public virtual IEnumerable<Block> ChildBlocks => [];

	internal SyntaxNode? Source { get; set; }
	internal TextSpan? LeadingLinesSpan { get; set; }
	internal Dictionary<string, TextSpan> Spans { get; } = [];
	internal StatementBaseline? Baseline { get; set; }
}

public sealed class LocalDecl : Statement {
	public LocalDecl(LuaGraph graph) : base(graph) { }

	public List<Binding> Names { get; } = [];
	public List<InputPin> Values { get; } = [];

	public Binding AddName(string name) {
		var pin = AddOutput(name);
		var binding = new Binding(name, BindingKind.Local, this, pin.Index);
		Names.Add(binding);
		return binding;
	}

	public InputPin AddValue() {
		var pin = AddInput("value" + (Values.Count + 1));
		Values.Add(pin);
		return pin;
	}
}

public sealed class AssignTarget {
	internal AssignTarget(TargetKind kind, Binding? binding, string? name, InputPin? obj, InputPin? key) {
		Kind = kind;
		Binding = binding;
		Name = name;
		Object = obj;
		Key = key;
	}

	public TargetKind Kind { get; }
	public Binding? Binding { get; }
	public string? Name { get; set; }
	public InputPin? Object { get; }
	public InputPin? Key { get; }
}

public sealed class AssignStatement : Statement {
	public AssignStatement(LuaGraph graph) : base(graph) { }

	public List<AssignTarget> Targets { get; } = [];
	public List<InputPin> Values { get; } = [];

	public AssignTarget AddBindingTarget(Binding binding) {
		var target = new AssignTarget(TargetKind.Binding, binding, binding.Name, null, null);
		Targets.Add(target);
		binding.Assignments++;
		return target;
	}

	public AssignTarget AddGlobalTarget(string name) {
		var target = new AssignTarget(TargetKind.Global, null, name, null, null);
		Targets.Add(target);
		return target;
	}

	public AssignTarget AddMemberTarget(string name) {
		var target = new AssignTarget(TargetKind.Member, null, name, AddInput("object"), null);
		Targets.Add(target);
		return target;
	}

	public AssignTarget AddIndexTarget() {
		var target = new AssignTarget(TargetKind.Index, null, null, AddInput("object"), AddInput("key"));
		Targets.Add(target);
		return target;
	}

	public InputPin AddValue() {
		var pin = AddInput("value" + (Values.Count + 1));
		Values.Add(pin);
		return pin;
	}
}

public sealed class CallStatement : Statement {
	public CallStatement(LuaGraph graph) : base(graph) {
		Call = AddInput("call");
	}

	public InputPin Call { get; }
	public CallNode? CallNode => Call.Source?.Owner as CallNode;
}

public sealed class IfClause {
	internal IfClause(InputPin condition, Block body) {
		Condition = condition;
		Body = body;
	}

	public InputPin Condition { get; }
	public Block Body { get; }
}

public sealed class IfStatement : Statement {
	public IfStatement(LuaGraph graph) : base(graph) { }

	public List<IfClause> Clauses { get; } = [];
	public Block? Else { get; set; }

	public IfClause AddClause() {
		var block = new Block(Clauses.Count == 0 ? "then" : "elseif") { Owner = this };
		var clause = new IfClause(AddInput(Clauses.Count == 0 ? "condition" : "condition" + (Clauses.Count + 1)), block);
		Clauses.Add(clause);
		return clause;
	}

	public Block AddElse() => Else ??= new Block("else") { Owner = this };

	public override IEnumerable<Block> ChildBlocks => Clauses.Select(c => c.Body).Concat(Else is null ? [] : [Else]);
}

public sealed class WhileStatement : Statement {
	public WhileStatement(LuaGraph graph) : base(graph) {
		Condition = AddInput("condition");
		Body = new Block("body") { Owner = this };
	}

	public InputPin Condition { get; }
	public Block Body { get; }
	public override IEnumerable<Block> ChildBlocks => [Body];
}

public sealed class RepeatStatement : Statement {
	public RepeatStatement(LuaGraph graph) : base(graph) {
		Condition = AddInput("until");
		Body = new Block("body") { Owner = this };
	}

	public InputPin Condition { get; }
	public Block Body { get; }
	public override IEnumerable<Block> ChildBlocks => [Body];
}

public sealed class NumericFor : Statement {
	public NumericFor(LuaGraph graph, string varName) : base(graph) {
		Var = new Binding(varName, BindingKind.LoopVar, this, AddOutput(varName).Index);
		Start = AddInput("start");
		Limit = AddInput("limit");
		Step = AddInput("step");
		Body = new Block("body") { Owner = this };
	}

	public Binding Var { get; }
	public InputPin Start { get; }
	public InputPin Limit { get; }

	public InputPin Step { get; }

	public Block Body { get; }
	public override IEnumerable<Block> ChildBlocks => [Body];
}

public sealed class GenericFor : Statement {
	public GenericFor(LuaGraph graph) : base(graph) {
		Body = new Block("body") { Owner = this };
	}

	public List<Binding> Vars { get; } = [];
	public List<InputPin> Exprs { get; } = [];
	public Block Body { get; }
	public override IEnumerable<Block> ChildBlocks => [Body];

	public Binding AddVar(string name) {
		var binding = new Binding(name, BindingKind.LoopVar, this, AddOutput(name).Index);
		Vars.Add(binding);
		return binding;
	}

	public InputPin AddExpr() {
		var pin = AddInput("iterator" + (Exprs.Count + 1));
		Exprs.Add(pin);
		return pin;
	}
}

public sealed class DoStatement : Statement {
	public DoStatement(LuaGraph graph) : base(graph) {
		Body = new Block("body") { Owner = this };
	}

	public Block Body { get; }
	public override IEnumerable<Block> ChildBlocks => [Body];
}

public sealed class FunctionDecl : Statement {
	public FunctionDecl(LuaGraph graph, bool isLocal, string name) : base(graph) {
		IsLocal = isLocal;
		Entry = new FunctionEntry(graph, this);
		Path = [name];
		Root = AddInput("table");
		if (isLocal) FunctionBinding = new Binding(name, BindingKind.LocalFunction, this, AddOutput(name).Index);
	}

	public bool IsLocal { get; }

	public List<string> Path { get; }

	public bool IsMethod { get; set; }

	public InputPin Root { get; }

	public Binding? AssignedBinding { get; internal set; }

	public Binding? FunctionBinding { get; private set; }

	internal void AdoptLocalBinding(Binding binding) {
		binding.Output.Binding = null;
		binding.OutputIndex = FunctionBinding!.OutputIndex;
		binding.Declarer = this;
		binding.Kind = BindingKind.LocalFunction;
		binding.Output.Binding = binding;
		FunctionBinding = binding;
	}
	public FunctionEntry Entry { get; }
	public string Name => IsLocal ? FunctionBinding!.Name : Path[^1];
	public override IEnumerable<Block> ChildBlocks => [Entry.Body];
}

public sealed class ReturnStatement : Statement {
	public ReturnStatement(LuaGraph graph) : base(graph) { }

	public List<InputPin> Values { get; } = [];

	public InputPin AddValue() {
		var pin = AddInput("value" + (Values.Count + 1));
		Values.Add(pin);
		return pin;
	}
}

public sealed class BreakStatement : Statement {
	public BreakStatement(LuaGraph graph) : base(graph) { }
}

public sealed class GotoStatement : Statement {
	public GotoStatement(LuaGraph graph, string label) : base(graph) {
		Label = label;
	}

	public string Label { get; set; }
}

public sealed class LabelStatement : Statement {
	public LabelStatement(LuaGraph graph, string name) : base(graph) {
		Name = name;
	}

	public string Name { get; set; }
}

public sealed class CastStatement : Statement {
	public Binding? Target { get; internal set; }

	public CastStatement(LuaGraph graph, string variable, string typeText) : base(graph) {
		Variable = variable;
		TypeText = typeText;
	}

	public string Variable { get; set; }

	public string TypeText { get; set; }

	internal TextSpan? LineSpan { get; set; }
}

public sealed class CommentStatement : Statement {
	public CommentStatement(LuaGraph graph, IEnumerable<string> lines) : base(graph) {
		Lines.AddRange(lines);
	}

	public List<string> Lines { get; } = [];
	internal TextSpan? LineSpan { get; set; }
}

public sealed class SequenceStatement : Statement {
	public SequenceStatement(LuaGraph graph) : base(graph) { }

	public List<Block> Branches { get; } = [];
	public override IEnumerable<Block> ChildBlocks => Branches;

	public Block AddBranch() {
		var block = new Block("branch") { Owner = this };
		Branches.Add(block);
		return block;
	}
}

public sealed class RawStatement : Statement {
	public RawStatement(LuaGraph graph, string text) : base(graph) {
		Text = text;
	}

	public string Text { get; set; }
}

public sealed record Wire(OutputPin From, InputPin To);

public sealed record GraphDiagnostic(DiagnosticSeverity Severity, string Message, GraphNode Node, InputPin? Pin);

internal sealed record PartBaseline(TextSpan Span, string Text, IReadOnlyList<FunctionNode>? Closures = null);

internal sealed record StatementBaseline(string Shape, IReadOnlyList<PartBaseline> Parts, IReadOnlyList<string> LeadingLines);
