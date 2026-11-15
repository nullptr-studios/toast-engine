using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Layout;
using editor.LuaEditor.Syntax;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Editing;

public readonly record struct InsertPoint(Block Block, int Index);

internal sealed record SessionSnapshot(string Text, GraphMeta Meta);

public sealed partial class GraphSession {
	private static readonly Regex s_number = new(@"^[+-]?(0[xX][0-9a-fA-F]+|\d+\.?\d*([eE][+-]?\d+)?|\.\d+([eE][+-]?\d+)?)$", RegexOptions.Compiled);
	private readonly List<SessionSnapshot> m_undo = [];
	private readonly List<SessionSnapshot> m_redo = [];
	private readonly TypeChecker m_checker;
	private SessionSnapshot m_current;
	private int m_savedStep;
	private int m_step;

	public GraphSession(LuaGraph graph, GraphLayout layout) {
		Graph = graph;
		Layout = layout;
		Universe = graph.Universe;
		Catalog = new NodeCatalog(Universe);
		m_checker = new TypeChecker(Universe);
		Graph.Analyze();
		m_current = TakeSnapshot() ?? new SessionSnapshot(graph.Document.Text, LayoutPersistence.Capture(layout));
	}

	public static GraphSession Open(LuaGraph graph, GraphMeta? meta, INodeMeasure? measure = null)
		=> new(graph, LayoutPersistence.Restore(graph, meta, measure));

	public LuaGraph Graph { get; private set; }
	public GraphLayout Layout { get; private set; }
	public TypeUniverse Universe { get; }
	public NodeCatalog Catalog { get; }
	public List<GraphNode> Selection { get; } = [];

	public string? Message { get; private set; }

	public void MarkDirty() {
		m_step++;
		Raise();
	}

	public bool CanUndo => m_undo.Count > 0;
	public bool CanRedo => m_redo.Count > 0;
	public bool IsDirty => m_step != m_savedStep;

	public event Action? Changed;
	public event Action? SelectionChanged;

	private void Raise() => Changed?.Invoke();

	private T WithoutFloating<T>(Func<T> action) {
		var held = Graph.Nodes.OfType<Statement>().Where(x => x.IsFloating && x.Parent is not null).Select(x => (Block: x.Parent!, Index: x.Parent!.IndexOf(x), Statement: x)).OrderBy(h => h.Index).ToList();
		foreach (var (block, _, statement) in held.AsEnumerable().Reverse()) block.Remove(statement);
		try {
			return action();
		} finally {
			foreach (var (block, index, statement) in held) block.Insert(Math.Min(index, block.Statements.Count), statement);
		}
	}

	private SaveResult SaveGraph() => WithoutFloating(() => Graph.Save());

	private SessionSnapshot? TakeSnapshot() {
		var saved = SaveGraph();

		return saved.Errors.Count > 0 ? null : new SessionSnapshot(saved.Text, LayoutPersistence.Capture(Layout));
	}

	private bool Edit(Action edit, bool structural = true) {
		var before = m_current;
		var activeFrames = Layout.Embedded.Select(frame => (frame.Key, frame.Value, Call: NodeShape.CallOf(frame.Value))).ToArray();
		try {
			edit();
		} catch (Exception e) when (e is InvalidOperationException or ArgumentException) {
			Message = e.Message;
			// failed edits can leave partially built nodes behind
			Restore(before);
			Raise();
			return false;
		}

		if (structural) {
			Layout.Refresh();
			if (activeFrames.Any(frame => !Layout.Embedded.TryGetValue(frame.Key, out var host) || host != frame.Value && (frame.Call is null || NodeShape.CallOf(host) != frame.Call) ||
				Layout.HostBody(host) is { } containing && Layout.Embedded.ContainsKey(containing))) {
				Message = "close the active POE frame first";
				Restore(before);
				Raise();
				return false;
			}
			LayoutEngine.PlaceUnplaced(Layout);
			LayoutEngine.PlaceNewRegions(Layout);
			Graph.Analyze();
			LayoutEngine.FollowHosts(Layout);
			var next = TakeSnapshot();
			if (next is null) {
				var errors = SaveGraph().Errors;
				Message = errors.Count > 0 ? errors[0].Message : "that edit cannot be written as Lua";
				Restore(before);
				Raise();
				return false;
			}
			m_current = next;
		} else {
			m_current = before with { Meta = LayoutPersistence.Capture(Layout) };
		}

		m_undo.Add(before);
		m_redo.Clear();
		m_step++;
		Message = null;
		Raise();
		return true;
	}

	private void Restore(SessionSnapshot snapshot) {
		var document = LuaDocument.Parse(snapshot.Text);
		if (!LuaGraph.TryBuild(document, Universe, out var graph, out _)) {
			Message = "could not rebuild the graph";
			return;
		}
		Graph = graph!;
		Graph.Analyze();
		Layout = LayoutPersistence.Restore(Graph, snapshot.Meta, Layout.Measure);
		m_current = snapshot;
		Selection.Clear();
	}

	public void Undo() {
		if (m_undo.Count == 0) return;
		var target = m_undo[^1];
		m_undo.RemoveAt(m_undo.Count - 1);
		m_redo.Add(m_current);
		Restore(target);
		m_step--;
		Raise();
	}

	public void Redo() {
		if (m_redo.Count == 0) return;
		var target = m_redo[^1];
		m_redo.RemoveAt(m_redo.Count - 1);
		m_undo.Add(m_current);
		Restore(target);
		m_step++;
		Raise();
	}

	public string? SaveText() {
		var result = SaveGraph();
		if (result.Errors.Count > 0) {
			Message = result.Errors[0].Message;
			return null;
		}
		return result.Text;
	}

	public GraphMeta CaptureLayout() => LayoutPersistence.Capture(Layout);

	public void MarkSaved(string writtenText) {
		var meta = LayoutPersistence.Capture(Layout);
		var selected = Selection.Select(Layout.Keys.KeyOf).OfType<string>().ToList();
		Restore(new SessionSnapshot(writtenText, meta));
		Selection.AddRange(selected.Select(Layout.Keys.NodeOf).OfType<GraphNode>());
		m_savedStep = m_step;
		Message = null;
		Raise();
	}

	public bool Reload(string text) {
		var document = LuaDocument.Parse(text);
		if (document.HasErrors) {
			Message = $"line {document.Errors[0].Location.Line}: {document.Errors[0].Message}";
			Raise();
			return false;
		}
		var meta = LayoutPersistence.Capture(Layout);
		Restore(new SessionSnapshot(text, meta));
		m_undo.Clear();
		m_redo.Clear();
		m_step = m_savedStep = 0;
		Message = null;
		Raise();
		return true;
	}

	public void Select(IEnumerable<GraphNode> nodes, bool additive = false) {
		var set = nodes.Select(InlineRules.Drawn).Distinct().ToList();
		if (!additive) Selection.Clear();
		foreach (var node in set) {
			if (additive && Selection.Contains(node)) Selection.Remove(node);
			else Selection.Add(node);
		}
		SelectionChanged?.Invoke();
		Raise();
	}

	public void ClearSelection() {
		Selection.Clear();
		SelectionChanged?.Invoke();
		Raise();
	}

	public void SelectAll() {
		Selection.Clear();
		foreach (var (node, _) in Layout.Keys.Ordered)
			if (!InlineRules.IsMerged(node) && node is not FunctionEntry) Selection.Add(node);
		Selection.AddRange(Layout.Loose);
		SelectionChanged?.Invoke();
		Raise();
	}

	public CompatResult CanConnect(OutputPin from, InputPin to) {
		if (from.Owner == to.Owner) return CompatResult.Block("a node cannot be wired to itself");
		if (to.Owner is Statement && to.Owner.Inputs.Contains(to) is false) return CompatResult.Block("that pin is gone");
		if (Reaches(from.Owner, to.Owner)) return CompatResult.Block("that would make a value depend on itself");
		var expected = to.Expected;
		if (expected is null || expected == LuaType.Unknown) return CompatResult.Ok;
		return m_checker.Check(from.Type == LuaType.Unknown ? LuaType.Any : from.Type, expected);
	}

	private static bool Reaches(GraphNode start, GraphNode target) {
		var seen = new HashSet<GraphNode>();
		var stack = new Stack<GraphNode>();
		stack.Push(start);
		while (stack.Count > 0) {
			var node = stack.Pop();
			if (!seen.Add(node)) continue;
			if (node == target) return true;
			foreach (var input in node.Inputs)
				if (input.Source is { Binding: null } source) stack.Push(source.Owner);
		}
		return false;
	}

	public bool Connect(OutputPin from, InputPin to, GraphNode? callableHost = null) {
		var compat = CanConnect(from, to);
		if (compat.Kind == Compat.Blocked) {
			Message = compat.Reason;
			Raise();
			return false;
		}
		return Edit(() => {
			ConnectCore(from, to);
			if (callableHost is null) Layout.CallableWireHosts.Remove(to);
			else Layout.CallableWireHosts[to] = callableHost;
		});
	}

	public bool ConnectFrameReturn(FunctionDecl function, int index, OutputPin from, GraphNode? callableHost = null) {
		var body = function.Entry.Body;
		if (!Layout.Embedded.ContainsKey(body) || index < 0) return false;
		if (NodeShape.TrailingReturn(body) is { } existing && index < existing.Values.Count)
			return Connect(from, existing.Values[index], callableHost);
		return Edit(() => {
			var ret = NodeShape.TrailingReturn(body) ?? Graph.Insert(body, body.Statements.Count, new ReturnStatement(Graph));
			while (ret.Values.Count <= index) ret.AddValue().SetConstant(LuaConstant.Nil);
			var pin = ret.Values[index];
			ConnectCore(from, pin);
			if (callableHost is not null) Layout.CallableWireHosts[pin] = callableHost;
		});
	}

	public bool ConnectFrameExit(FunctionDecl function, Statement statement) {
		var body = function.Entry.Body;
		if (!Layout.Embedded.ContainsKey(body)) return false;
		var wasFloating = statement.IsFloating;
		var at = Layout.Position(statement);
		var ok = Edit(() => {
			var ret = NodeShape.TrailingReturn(body);
			if (statement is ReturnStatement && ret is not null && ret != statement)
				throw new InvalidOperationException("the frame already has a return. Use the value inputs on its right rail");
			statement.IsFloating = false;
			Graph.Move(statement, body, ret is not null && ret != statement ? body.IndexOf(ret) : body.Statements.Count);
		});
		if (ok && wasFloating) {
			AdoptLoose(statement, at);
			m_current = m_current with { Meta = LayoutPersistence.Capture(Layout) };
			Raise();
		}
		return ok;
	}

	private void ConnectCore(OutputPin from, InputPin to) {
		if (from.Owner is CallNode call && call.OwnerStatement is CallStatement statement && statement.Call.Source?.Owner == call) {
			if (statement.IsFloating) throw new InvalidOperationException("wire the call into the flow before using its result");
			var count = Math.Max(call.ReturnCount, from.Index + 1);
			var used = Graph.Nodes.SelectMany(node => node.Outputs).Select(pin => pin.Binding?.Name).OfType<string>()
				.Concat(Graph.Nodes.OfType<GlobalGet>().Select(global => global.Name)).ToHashSet();
			var names = new List<string>();
			for (var i = 1; names.Count < count; i++) {
				var name = i == 1 ? "result" : "result" + i;
				if (used.Add(name)) names.Add(name);
			}
			var capture = Graph.CaptureCallResults(statement, names);
			if (Layout.Local.Remove(statement, out var position)) Layout.Local[capture] = position;
			if (Layout.PinnedNodes.Remove(statement)) Layout.PinnedNodes.Add(capture);
			if (Layout.ExecReroutes.Remove(statement, out var bends)) Layout.ExecReroutes[capture] = bends;
			foreach (var pin in Layout.CallableWireHosts.Where(wire => wire.Value == statement).Select(wire => wire.Key).ToList())
				Layout.CallableWireHosts[pin] = capture;
			foreach (var group in Layout.Groups) {
				var index = group.Members.IndexOf(statement);
				if (index >= 0) group.Members[index] = capture;
			}
			if (statement.IsExpanded && NodeShape.LocalCalleeOf(capture) is { } callee) {
				capture.IsExpanded = true;
				Layout.Embedded[callee.Entry.Body] = capture;
			}
			var selection = Selection.IndexOf(statement);
			if (selection >= 0) Selection[selection] = capture;
			from = capture.Names[from.Index].Output;
		}
		if (from.Owner is RawExpression raw && ParseConstant(raw.Text, null) is { } constant) {
			Graph.SetConstant(to, constant);
			Layout.Loose.Remove(raw);
			Layout.Local.Remove(raw);
			Graph.Unregister(raw);
			return;
		}
		var wasLoose = from.Owner is ExprNode && Layout.Loose.Contains(from.Owner);
		var abs = wasLoose ? Layout.Position(from.Owner) : default;
		Graph.Connect(from, to);
		if (wasLoose) AdoptLoose(from.Owner, abs);
	}

	private void AdoptLoose(GraphNode node, Vec absolute) {
		Layout.Refresh();
		Layout.Loose.Remove(node);
		Layout.SetPosition(node, absolute);
		foreach (var input in node.Inputs)
			if (input.Source?.Owner is { } child && Layout.Loose.Contains(child)) AdoptLoose(child, Layout.Position(child));
	}

	public bool Disconnect(InputPin pin) => Edit(() => Graph.Disconnect(pin));

	public bool MoveWires(OutputPin from, OutputPin to) => Edit(() => {
		foreach (var pin in from.Consumers.ToList()) ConnectCore(to, pin);
	});

	public bool SetConstant(InputPin pin, string text) {
		var constant = ParseConstantFor(pin, text);
		if (constant is null) {
			Message = $"\"{text}\" is not a value that fits here";
			Raise();
			return false;
		}
		return Edit(() => Graph.SetConstant(pin, constant));
	}

	public static LuaConstant? ParseConstantFor(InputPin pin, string text) {
		var wantsText = pin.Constant?.Kind == ConstantKind.String || IsTextType(pin.Expected);
		if (!wantsText) return ParseConstant(text, pin.Expected);
		var t = text.Trim();
		if (t.Length >= 2 && ((t[0] == '"' && t[^1] == '"') || (t[0] == '\'' && t[^1] == '\''))) return new LuaConstant(ConstantKind.String, t);
		return LuaConstant.String(text);
	}

	private static bool IsTextType(LuaType? type) => type switch {
		PrimitiveType { Kind: PrimitiveKind.String } => true,
		LiteralType { Value: string } => true,
		UnionType u => u.Members.Any(m => m is not PrimitiveType { Kind: PrimitiveKind.Nil }) && u.Members.All(m => m is PrimitiveType { Kind: PrimitiveKind.String or PrimitiveKind.Nil } or LiteralType { Value: string }),
		_ => false
	};

	public static string DisplayText(LuaConstant constant) {
		if (constant.Kind != ConstantKind.String || constant.Raw.Length < 2) return constant.Raw;
		var inner = constant.Raw[1..^1];
		if (inner.StartsWith("[", StringComparison.Ordinal)) return constant.Raw;
		return inner.Replace("\\\"", "\"").Replace("\\n", "\n").Replace("\\t", "\t").Replace("\\\\", "\\");
	}

	public sealed record PinChoice(string Label, bool IsCurrent, Action Apply);

	public IReadOnlyList<PinChoice>? ChoicesFor(InputPin pin) {
		if (pin.Expected is not { } expected) return null;
		var resolved = Universe.ResolveAliases(expected);
		if (resolved is NamedType named && Universe.FindEnum(named.Name) is { } decl) {
			var current = InlineRules.EnumValueOf(pin);
			return decl.Members.Select(m => new PinChoice(m.Name, m.Name == current, () => Create(new NodeEntry(NodeKind.Member, decl.TableName + "." + m.Name, "", "", Name: m.Name, Owner: decl.TableName), default, into: pin))).ToList();
		}
		if (resolved is UnionType union) {
			var options = union.Members.Where(m => m is not PrimitiveType { Kind: PrimitiveKind.Nil }).ToList();
			if (options.Count > 1 && options.All(m => m is LiteralType)) {
				return options.Cast<LiteralType>().Select(l => {
					var constant = l.Value is string s ? LuaConstant.String(s) : l.Value is bool b ? (b ? LuaConstant.True : LuaConstant.False) : LuaConstant.Number(Convert.ToString(l.Value, System.Globalization.CultureInfo.InvariantCulture) ?? "0");
					return new PinChoice(DisplayText(constant), pin.Constant == constant, () => SetConstantValue(pin, constant));
				}).ToList();
			}
		}
		return null;
	}

	public bool SetConstantValue(InputPin pin, LuaConstant constant) => Edit(() => Graph.SetConstant(pin, constant));

	public static bool IsBoolean(InputPin pin) => pin.Constant?.Kind == ConstantKind.Boolean || pin.Expected is PrimitiveType { Kind: PrimitiveKind.Boolean };

	public bool Toggle(InputPin pin) => SetConstantValue(pin, pin.Constant?.Raw == "true" ? LuaConstant.False : LuaConstant.True);

	public static LuaConstant? ParseConstant(string text, LuaType? expected) {
		var t = text.Trim();
		if (t == "nil") return LuaConstant.Nil;
		if (t == "true") return LuaConstant.True;
		if (t == "false") return LuaConstant.False;
		if (s_number.IsMatch(t)) return LuaConstant.Number(t);
		if (t.Length >= 2 && ((t[0] == '"' && t[^1] == '"') || (t[0] == '\'' && t[^1] == '\''))) return new LuaConstant(ConstantKind.String, t);
		if (expected is PrimitiveType { Kind: PrimitiveKind.String } || expected is null) return expected is null ? null : LuaConstant.String(text);
		return null;
	}

	public GraphNode? Create(NodeEntry entry, Vec at, OutputPin? from = null, InputPin? into = null, InsertPoint? insert = null) {
		GraphNode? created = null;
		var statementCall = entry.Kind is NodeKind.CallGlobal or NodeKind.CallMethod && into is null;
		var ok = Edit(() => {
			var standalone = from is null || from.Owner.OwnerStatement is null;
			var floating = (entry.IsStatement || statementCall) && insert is null && standalone;
			created = entry.IsStatement || statementCall
				? CreateStatement(entry, at, from, insert ?? (standalone ? FloatingPlace(at) : InsertionFor(from, at)))
				: CreateValue(entry, at, from, into);
			if (floating && created is Statement made) {
				made.IsFloating = true;
				Layout.Loose.Add(made);
				Layout.Local[made] = at;
			}
		});

		if (!ok) return null;
		if (created is not null) {
			Layout.Refresh();
			if (InlineRules.Drawn(created) is var drawn && Layout.IsPlaced(drawn)) {
				var delta = at - Layout.Position(drawn);
				if (drawn is Statement or ExprNode) MoveGroup(drawn, delta);
				LayoutEngine.Separate(Layout, [drawn]);
			}
			Selection.Clear();
			Selection.Add(InlineRules.Drawn(created));
			SelectionChanged?.Invoke();
			RetidyFrameOf(created);
			Raise();
		}
		return created;
	}

	private Statement CreateStatement(NodeEntry entry, Vec at, OutputPin? from, InsertPoint point) {
		Statement statement = entry.Kind switch {
			NodeKind.If => MakeIf(),
			NodeKind.While => MakeWhile(),
			NodeKind.Repeat => MakeRepeat(),
			NodeKind.NumericFor => MakeNumericFor(point),
			NodeKind.GenericFor => MakeGenericFor(point, entry.Name == "pairs"),
			NodeKind.SetIndex => MakeSetIndex(),
			NodeKind.SetMember => MakeSetMember(entry),
			NodeKind.Do => new DoStatement(Graph),
			NodeKind.Return => new ReturnStatement(Graph),
			NodeKind.Break => new BreakStatement(Graph),
			NodeKind.Sequence => MakeSequence(),
			NodeKind.Local => MakeLocal(point),
			NodeKind.Cast => new CastStatement(Graph, from?.Binding?.Name ?? from?.Name ?? "value", "any"),
			NodeKind.Comment => new CommentStatement(Graph, ["-- comment"]),
			NodeKind.RawStatement => new RawStatement(Graph, "do end"),
			NodeKind.SetVariable => MakeSet(entry),
NodeKind.CallGlobal or NodeKind.CallMethod => MakeCallStatement(entry, point.Block),
			_ => throw new InvalidOperationException("that node is not a statement")
		};
		BringLocalCalleeIntoScope(statement, point.Block);
		Graph.Insert(point.Block, point.Index, statement);
		if (statement is AssignStatement { Targets: [{ Object: { } objectPin }] } && entry.Kind == NodeKind.SetVariable && ObjectSource(point.Block) is { } owner) ConnectCore(owner, objectPin);
		if (from is not null && PrimaryInput(statement) is { } pin) ConnectCore(from, pin);
		return statement;
	}

	private IfStatement MakeIf() {
		var statement = new IfStatement(Graph);
		statement.AddClause().Condition.SetConstant(LuaConstant.True);
		return statement;
	}

	private WhileStatement MakeWhile() {
		var statement = new WhileStatement(Graph);
		statement.Condition.SetConstant(LuaConstant.True);
		return statement;
	}

	private RepeatStatement MakeRepeat() {
		var statement = new RepeatStatement(Graph);
		statement.Condition.SetConstant(LuaConstant.True);
		return statement;
	}

	private NumericFor MakeNumericFor(InsertPoint point) {
		var statement = new NumericFor(Graph, FreeName(point, ["i", "j", "k", "n"]));
		statement.Start.SetConstant(LuaConstant.Integer(1));
		statement.Limit.SetConstant(LuaConstant.Integer(10));
		return statement;
	}

	private AssignStatement MakeSetIndex() {
		var assign = new AssignStatement(Graph);
		assign.AddIndexTarget();
		assign.AddValue();
		return assign;
	}

	private AssignStatement MakeSetMember(NodeEntry entry) {
		var assign = new AssignStatement(Graph);
		assign.AddMemberTarget(entry.Name ?? "name");
		assign.AddValue();
		return assign;
	}

	private GenericFor MakeGenericFor(InsertPoint point, bool pairs = false) {
		var statement = new GenericFor(Graph);
		statement.AddVar(FreeName(point, pairs ? ["key", "k", "_k"] : ["_", "_k", "_i"]));
		statement.AddVar(FreeName(point, ["value", "item", "entry"]));
		var call = new CallNode(Graph, CallStyle.Function, null);
		call.Target.Connect(new GlobalGet(Graph, pairs ? "pairs" : "ipairs").Result);

		call.AddArg("t");
		statement.AddExpr().Connect(call.Result);
		return statement;
	}

	private SequenceStatement MakeSequence() {
		var statement = new SequenceStatement(Graph);
		statement.AddBranch();
		statement.AddBranch();
		return statement;
	}

	private LocalDecl MakeLocal(InsertPoint point) => Graph.NewLocal(FreeName(point, ["value", "temp", "result", "item"]));

	private CallStatement MakeCallStatement(NodeEntry entry, Block block) {
		var call = (CallNode)BuildCall(entry);
		BindHierarchyCall(entry, call, block);
		var statement = new CallStatement(Graph);
		statement.Call.Connect(call.Result);
		return statement;
	}

	private string FreeName(InsertPoint point, string[] candidates) {
		var visible = LuaGraph.VisibleFromBlock(point.Block, Math.Min(point.Index, point.Block.Statements.Count)).Select(b => b.Name).ToHashSet();
		foreach (var name in candidates)
			if (!visible.Contains(name)) return name;
		for (var i = 2;; i++)
			if (!visible.Contains(candidates[0] + i)) return candidates[0] + i;
	}

	private GraphNode? CreateValue(NodeEntry entry, Vec at, OutputPin? from, InputPin? into) {
		if (entry.Kind == NodeKind.GetVariable) return GetVariable(entry, at, from, into);
		if (entry.Kind == NodeKind.GetSelf) return GetSelf(entry, at, into);
		if (entry.Kind == NodeKind.Lambda) return MakeLambda(at, into);
		if (into is not null && entry.Kind is NodeKind.Number or NodeKind.String or NodeKind.Boolean or NodeKind.Nil) {
			var literal = entry.Kind switch {
				NodeKind.Number => LuaConstant.Integer(0),
				NodeKind.String => LuaConstant.String(""),
				NodeKind.Boolean => LuaConstant.False,
				_ => LuaConstant.Nil
			};
			Graph.SetConstant(into, literal);
			return new RawExpression(Graph, literal.Raw);
		}

		ExprNode node = entry.Kind switch {
			NodeKind.Binary => new BinaryOp(Graph, entry.Binary!.Value),
			NodeKind.Unary => new UnaryOp(Graph, entry.Unary!.Value),
			NodeKind.Number => new RawExpression(Graph, "0"),
			NodeKind.String => new RawExpression(Graph, "\"\""),
			NodeKind.Boolean => new RawExpression(Graph, "false"),
			NodeKind.Nil => new RawExpression(Graph, "nil"),
NodeKind.MakeTable => MakeTable(entry),
			NodeKind.RawExpression => new RawExpression(Graph, "nil"),
			NodeKind.Index => new IndexGet(Graph),
			NodeKind.GetGlobal => new GlobalGet(Graph, entry.Name!),
			NodeKind.Member => MakeMember(entry),
			NodeKind.CallGlobal or NodeKind.CallMethod or NodeKind.VectorConstructor => BuildCall(entry),
			_ => throw new InvalidOperationException("that node is not a value")
		};

		if (node is CallNode scriptCall) BindHierarchyCall(entry, scriptCall, into?.Owner.OwnerStatement?.Parent ?? BlockAt(at));
		if (from is not null && PrimaryInput(node) is { } input) ConnectCore(from, input);
		if (into is not null && entry.Kind is not (NodeKind.Number or NodeKind.String or NodeKind.Boolean or NodeKind.Nil)) ConnectCore(node.Result, into);
		else if (into is null) {
			Layout.Loose.Add(node);
			Layout.Local[node] = at;
		}
		return node;
	}

	private GraphVariable VariableOf(NodeEntry entry) {
		if (entry.Binding is { Declarer: Statement declaration } binding)
			return new GraphVariable(binding.Name, VariableKind.ModuleLocal, false, binding.Type, null, "", declaration);
		if (entry.Owner == BaseOwner && ScriptInfo.Find(Graph) is { } script) return new GraphVariable(entry.Name!, VariableKind.Field, true, entry.Result ?? entry.FirstInput ?? LuaType.Any, null, entry.Doc, script.Declaration);
		return Graph.Variables.FirstOrDefault(v => v.Name == entry.Name && v.Kind.ToString() == entry.Owner) ?? throw new InvalidOperationException($"there is no variable called {entry.Name}");
	}

	public const string BaseOwner = "base";

	public string? BaseClassName => ScriptInfo.Find(Graph)?.ClassTag?.Bases.OfType<NamedType>().FirstOrDefault()?.Name;

	public IReadOnlyList<MemberInfo> BaseMembers() => BaseClassName is { } name ? Universe.MembersOf(name) : [];

	private AssignStatement MakeSet(NodeEntry entry) {
		var variable = VariableOf(entry);
		var assign = new AssignStatement(Graph);
		if (variable.Kind == VariableKind.Field) assign.AddMemberTarget(variable.Name);
		else assign.AddBindingTarget(entry.Binding ?? ((LocalDecl)variable.Declaration).Names.First(n => n.Name == variable.Name));
		assign.AddValue();
		return assign;
	}

	private GraphNode? GetVariable(NodeEntry entry, Vec at, OutputPin? from, InputPin? into) {
		var variable = VariableOf(entry);
		if (variable.Kind == VariableKind.Field) {
			var member = new MemberGet(Graph, variable.Name);
			var block = into?.Owner.OwnerStatement?.Parent ?? BlockAt(at);
				if (from is not null) ConnectCore(from, member.Object);
				else if (ObjectSource(block) is { } owner) ConnectCore(owner, member.Object);
			if (into is not null) ConnectCore(member.Result, into);
			else {
				Layout.Loose.Add(member);
				Layout.Local[member] = at;
			}
			return member;
		}
		var binding = entry.Binding ?? ((LocalDecl)variable.Declaration).Names.First(n => n.Name == variable.Name);
		if (into is null) {
			var read = new VariableGet(Graph, binding);
			Layout.Loose.Add(read);
			Layout.Local[read] = at;
			return read;
		}
		ConnectCore(binding.Output, into);
		return null;
	}

	private OutputPin? ObjectSource(Block? block) {
		for (var b = block; b is not null;) {
			if (b.Owner is FunctionEntry { Self: { } self }) return self.Output;
			var owner = b.Owner is FunctionEntry fe ? (fe.Owner as Statement ?? fe.Owner.OwnerStatement) : b.Owner as Statement;
			b = owner?.Parent;
		}
		return ScriptInfo.Find(Graph)?.Table.Output;
	}

	private TableNode MakeTable(NodeEntry entry) {
		var table = new TableNode(Graph);
		switch (entry.Name) {
			case "array":
				table.DeclaredFlavor = TableFlavor.Array;
				table.AddPositional();
				break;
			case "dictionary":
				table.DeclaredFlavor = TableFlavor.Dictionary;
				table.AddKeyed().Key!.SetConstant(LuaConstant.String("key"));
				break;
			default:
				table.DeclaredFlavor = TableFlavor.Table;
				table.AddNamed("field");
				break;
		}
		return table;
	}

	public bool AddTableEntry(TableNode table, TableFieldKind kind) => Edit(() => {
		switch (kind) {
			case TableFieldKind.Positional:
				table.AddPositional();
				break;
			case TableFieldKind.Named:
				var taken = table.Fields.Select(f => f.Name).ToHashSet();
				var name = "field";
				for (var i = 2; taken.Contains(name); i++) name = "field" + i;
				table.AddNamed(name);
				break;
			default:
				var keys = table.Fields.Where(f => f.Key is not null).Select(f => NodeShape.KeyText(f.Key!)).ToHashSet();
				var key = "key";
				for (var i = 2; keys.Contains(key); i++) key = "key" + i;
				table.AddKeyed().Key!.SetConstant(LuaConstant.String(key));
				break;
		}
	});

	public static TableFieldKind EntryKindOf(TableNode table) => table.Flavor switch {
		TableFlavor.Array => TableFieldKind.Positional,
		TableFlavor.Dictionary => TableFieldKind.Keyed,
		_ => TableFieldKind.Named
	};

	public bool SetTableKey(TableNode table, TableField field, string text) => Edit(() => {
		if (field.Key is null) throw new InvalidOperationException("only a dictionary entry has a key");
		text = text.Trim();
		if (text.Length == 0) throw new InvalidOperationException("a key cannot be empty");
		if (table.Fields.Any(f => f != field && f.Key is { } other && NodeShape.KeyText(other) == text)) throw new InvalidOperationException($"the dictionary already has the key {text}");
		var numeric = double.TryParse(text, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out _);
		// numeric text can still be a string key
		var keyType = (table.TargetType as TableType)?.Key;
		if (keyType is PrimitiveType { Kind: PrimitiveKind.Number or PrimitiveKind.Integer } && !numeric) throw new InvalidOperationException($"the keys of this dictionary are numbers, {text} is not one");
		var asString = keyType is PrimitiveType { Kind: PrimitiveKind.String } || !numeric;
		Graph.SetConstant(field.Key, asString ? LuaConstant.String(text) : LuaConstant.Number(text));
	});

	public bool RenameAccess(GraphNode node, string name) => Edit(() => {
		if (!LuaWriter.IsIdentifier(name)) throw new InvalidOperationException($"\"{name}\" cannot be used as a field name");
		switch (node) {
			case MemberGet member:
				member.Name = name;
				member.Result.Name = name;
				break;
			case AssignStatement { Targets: [{ Kind: TargetKind.Member } target] }:
				target.Name = name;
				break;
			default:
				throw new InvalidOperationException("that node does not read or write a field by name");
		}
	});

	public static TableNode? TableOfField(GraphVariable variable) => (variable.Declaration as AssignStatement)?.Values.FirstOrDefault()?.Source?.Owner as TableNode;

	public static string TableEntryType(TableField field) {
		if (field.Value.Source?.Owner is TableNode inner) return inner.Flavor switch {
			TableFlavor.Array => inner.DeclaredType is ArrayType chosen ? TypeFormatter.Format(chosen) : inner.Result.Type is ArrayType made ? TypeFormatter.Format(made) : "any[]",
			TableFlavor.Dictionary => inner.DeclaredType is TableType chosen ? TypeFormatter.Format(chosen) : inner.Result.Type is TableType made ? TypeFormatter.Format(made) : "table<string, any>",
			_ => "table"
		};
		if (field.Value.Source is { } source) return TypeFormatter.Display(source.Type);
		return field.Value.Constant?.Kind switch {
			ConstantKind.Number => "number",
			ConstantKind.String => "string",
			ConstantKind.Boolean => "boolean",
			_ => "any"
		};
	}

	public bool AddFieldEntry(InputPin fieldValue, TableFieldKind kind) => Edit(() => {
		var table = fieldValue.Source?.Owner as TableNode;
		if (table is null) {
			table = new TableNode(Graph);
			Graph.Connect(table.Result, fieldValue);
		}
		switch (kind) {
			case TableFieldKind.Positional:
				table.AddPositional();
				break;
			case TableFieldKind.Named:
				var taken = table.Fields.Select(f => f.Name).ToHashSet();
				var name = "field";
				for (var i = 2; taken.Contains(name); i++) name = "field" + i;
				table.AddNamed(name);
				break;
			default:
				table.AddKeyed().Key!.SetConstant(LuaConstant.String("key"));
				break;
		}
	});

	public bool SetTableEntryType(TableNode table, int index, string typeText) => Edit(() => {
		if (index < 0 || index >= table.Fields.Count) return;
		var pin = table.Fields[index].Value;
		var type = TypeExprParser.Parse(string.IsNullOrWhiteSpace(typeText) ? "any" : typeText);
		switch (type) {
			case PrimitiveType { Kind: PrimitiveKind.Number or PrimitiveKind.Integer }:
				Graph.SetConstant(pin, LuaConstant.Integer(0));
				break;
			case PrimitiveType { Kind: PrimitiveKind.String }:
				Graph.SetConstant(pin, LuaConstant.String(""));
				break;
			case PrimitiveType { Kind: PrimitiveKind.Boolean }:
				Graph.SetConstant(pin, LuaConstant.False);
				break;
			case ArrayType or TableType or PrimitiveType { Kind: PrimitiveKind.Table }:
				var made = pin.Source?.Owner as TableNode;
				if (made is null) {
					Graph.Disconnect(pin);
					made = new TableNode(Graph);
					Graph.Connect(made.Result, pin);
				}
				made.DeclaredFlavor = type switch { ArrayType => TableFlavor.Array, TableType => TableFlavor.Dictionary, _ => TableFlavor.Table };
				made.DeclaredType = type is ArrayType or TableType ? type : null;
				break;
			default:
				Graph.SetConstant(pin, LuaConstant.Nil);
				break;
		}
	});

	public bool RemoveTableEntry(TableNode table, int index) => Edit(() => {

		if (index < 0 || index >= table.Fields.Count) return;
		var field = table.Fields[index];
		if (field.Key is not null) Graph.Disconnect(field.Key);
		Graph.Disconnect(field.Value);
		table.RemoveFieldAt(index);
		var number = 1;
		foreach (var f in table.Fields.Where(f => f.Kind == TableFieldKind.Positional)) f.Value.Name = "[" + number++ + "]";
	});

	public bool RenameTableField(TableNode table, TableField field, string name) => Edit(() => {
		if (!LuaWriter.IsIdentifier(name)) throw new InvalidOperationException($"\"{name}\" cannot be used as a field name");
		if (table.Fields.Any(f => f != field && f.Name == name)) throw new InvalidOperationException($"the table already has a field called {name}");
		field.Name = name;
		field.Value.Name = name;
	});

	private MemberGet MakeMember(NodeEntry entry) {
		var member = new MemberGet(Graph, entry.Name!);
		if (entry.Owner is { } owner && (Universe.GlobalType(owner) is not null || Universe.Enums.Any(e => e.TableName == owner))) member.Object.Connect(new GlobalGet(Graph, owner).Result);
		return member;
	}

	private ExprNode BuildCall(NodeEntry entry) {
		var function = entry.Signature ?? FunctionOf(entry);
		if (entry.Kind == NodeKind.CallMethod) {
			var method = Graph.NewMethodCall(entry.Name!);
			foreach (var p in function?.Params ?? []) method.AddArg(p.Name);
			return method;
		}

		var call = new CallNode(Graph, CallStyle.Function, null);
		if (entry.Binding is { } binding) call.Target.Connect(binding.Output);
		if (entry.Binding is not null || entry.DotCall) {
			foreach (var param in function?.Params ?? []) call.AddArg(param.Name);
			return call;
		}
		var path = entry.Name!.Split('.');
		var target = new GlobalGet(Graph, path[0]).Result;
		foreach (var part in path.Skip(1)) {
			var member = new MemberGet(Graph, part);
			member.Object.Connect(target);
			target = member.Result;
		}
		call.Target.Connect(target);
		if (entry.Kind == NodeKind.VectorConstructor) {
			var names = entry.Name == "quat" ? new[] { "x", "y", "z", "w" } : new[] { "x", "y", "z" };
			for (var i = 0; i < entry.Arity; i++) call.AddArg(names[i]).SetConstant(LuaConstant.Integer(i == 3 ? 1 : 0));
			return call;
		}
		foreach (var p in function?.Params ?? []) call.AddArg(p.Name);
		if (function?.VarargParam is not null) call.AddArg("value");
		return call;
	}

	private FunctionType? FunctionOf(NodeEntry entry) {
		if (entry.Owner == ScriptOwner) return Graph.Variables.FirstOrDefault(v => v.Name == entry.Name && v.Kind == VariableKind.Function)?.Type as FunctionType;
		if (entry.Kind == NodeKind.CallMethod)
			return Universe.MembersOf(entry.Owner!).FirstOrDefault(m => m.Decl.Name == entry.Name)?.Decl.Type as FunctionType;
		var path = entry.Name!.Split('.');
		if (path.Length == 1) return Universe.GlobalType(path[0]) as FunctionType;
		var type = Universe.GlobalType(path[0]);
		return type is NamedType named ? Universe.MembersOf(named.Name).FirstOrDefault(m => m.Decl.Name == path[1])?.Decl.Type as FunctionType : null;
	}

	public IReadOnlyList<NodeEntry> SelfEntries() {
		var table = ScriptInfo.Find(Graph)?.Table.Name;
		var list = new List<NodeEntry> { new(NodeKind.GetSelf, "self", "Variables", "self this object me", Name: "self") };
		if (table is not null) list.Add(new NodeEntry(NodeKind.GetSelf, table, "Variables", table + " script table module", Name: "table"));
		return list;
	}

	private Block? BlockAt(Vec at) {
		foreach (var (body, host) in Layout.Embedded)
			if (Layout.IsPlaced(host) && Layout.Bounds(host) is var frame &&
				at.X >= frame.X && at.X <= frame.Right && at.Y >= frame.Y && at.Y <= frame.Bottom) return body;
		foreach (var region in Layout.Keys.Regions)
			if (!region.IsModule && Layout.RegionExtent(region) is { } extent && at.X >= extent.X && at.X <= extent.Right && at.Y >= extent.Y && at.Y <= extent.Bottom) return region.Body;
		return null;
	}

	private GraphNode? MakeLambda(Vec at, InputPin? into) {
		var wanted = into?.Expected is UnionType u && u.Members.Count(m => m != LuaType.Nil) == 1 ? u.Members.First(m => m != LuaType.Nil) : into?.Expected;
		var names = new List<string>();
		if (wanted is FunctionType signature) {
			for (var i = 0; i < signature.Params.Count; i++) {
				var name = signature.Params[i].Name;
				names.Add(LuaWriter.IsIdentifier(name) && !names.Contains(name) ? name : "arg" + (i + 1));
			}
			if (signature.VarargParam is not null) names.Add("...");
		}
		var nodes = Fragments.BuildLoose(Graph, "function(" + string.Join(", ", names) + ")\nend");
		if (nodes.Count == 0 || nodes[0] is not FunctionNode function) throw new InvalidOperationException("could not make the function");
		if (into is not null) {
			ConnectCore(function.Result, into);
		} else {
			Layout.Loose.Add(function);
			Layout.Local[function] = at;
		}
		return function;
	}

	private GraphNode? GetSelf(NodeEntry entry, Vec at, InputPin? into) {

		var source = entry.Name == "self" ? ObjectSource(into?.Owner.OwnerStatement?.Parent ?? BlockAt(at)) : ScriptInfo.Find(Graph)?.Table.Output;
		if (source?.Binding is not { } binding) throw new InvalidOperationException("there is no " + (entry.Name == "self" ? "self" : "script table") + " in this script");
		if (into is not null) {
			ConnectCore(source, into);
			return null;
		}
		var read = new VariableGet(Graph, binding);
		Layout.Loose.Add(read);
		Layout.Local[read] = at;
		return read;
	}

	public IReadOnlyList<NodeEntry> VariableEntries() => Graph.Variables.Where(v => v.Kind != VariableKind.Function).SelectMany(v => new[] {
		new NodeEntry(NodeKind.GetVariable, "Get " + v.Name, "Variables", "get " + v.Name, Name: v.Name, Owner: v.Kind.ToString(), Result: v.Type),
		new NodeEntry(NodeKind.SetVariable, "Set " + v.Name, "Variables", "set " + v.Name, Name: v.Name, Owner: v.Kind.ToString(), FirstInput: v.Type)
	}).Concat(BaseMembers().Where(m => m.Decl.Type is not FunctionType).SelectMany(m => new[] {
		new NodeEntry(NodeKind.GetVariable, "Get " + m.Decl.Name, "C++ " + m.DeclaringClass, "get " + m.Decl.Name + " c++", Name: m.Decl.Name, Owner: BaseOwner, Result: m.Decl.Type, Doc: m.Decl.Doc),
		new NodeEntry(NodeKind.SetVariable, "Set " + m.Decl.Name, "C++ " + m.DeclaringClass, "set " + m.Decl.Name + " c++", Name: m.Decl.Name, Owner: BaseOwner, FirstInput: m.Decl.Type, Doc: m.Decl.Doc)
	})).ToList();

	public const string ScriptOwner = "script";

	public IReadOnlyList<NodeEntry> FunctionEntries() => Graph.Variables.Where(v => v.Kind == VariableKind.Function && v.Declaration is FunctionDecl { IsMethod: true }).Select(v =>
		new NodeEntry(NodeKind.CallMethod, v.Name + "()", "Script functions", "call " + v.Name, Name: v.Name, Owner: ScriptOwner, Result: (v.Type as FunctionType)?.Returns.FirstOrDefault(), Doc: v.Doc))
		.Concat(BaseMembers().Where(m => m.Decl.Type is FunctionType && m.Decl.IsMethod).Select(m =>
			new NodeEntry(NodeKind.CallMethod, m.Decl.Name + "()", "C++ " + m.DeclaringClass, "call " + m.Decl.Name + " c++", Name: m.Decl.Name, Owner: m.DeclaringClass, Result: (m.Decl.Type as FunctionType)?.Returns.FirstOrDefault(), Doc: m.Decl.Doc)))
		.ToList();

	private static bool IsOwnCall(NodeEntry entry) => entry.Owner == ScriptOwner || entry.Category.StartsWith("C++ ", StringComparison.Ordinal);

	private bool IsScriptObject(LuaType type) => ScriptInfo.Find(Graph)?.ClassName is { } name && type is NamedType named && named.Name == name;

	public NodeEntry? ScriptFunctionEntry(string name) => FunctionEntries().FirstOrDefault(e => e.Name == name);

	public NodeEntry? VariableEntry(string name, bool set) => VariableEntries().FirstOrDefault(e => e.Name == name && (e.Kind == NodeKind.SetVariable) == set);

	public IReadOnlyList<NodeEntry> Search(string text, LuaType? producing = null, LuaType? expecting = null, bool statementsOnly = false) {
		var words = text.Split(' ', StringSplitOptions.RemoveEmptyEntries);
		var own = VariableEntries().Where(e => words.All(w => e.Title.Contains(w, StringComparison.OrdinalIgnoreCase) || e.Search.Contains(w, StringComparison.OrdinalIgnoreCase)))
			.Where(e => producing is null || e.Kind == NodeKind.SetVariable || e.Kind == NodeKind.GetVariable && e.Owner == nameof(VariableKind.Field))
			.Where(e => expecting is null || e.Kind == NodeKind.GetVariable && (e.Result is null || m_checker.Check(e.Result, expecting).Kind != Compat.Blocked))
			.Where(e => !statementsOnly || e.Kind == NodeKind.SetVariable);
		var functions = FunctionEntries().Where(e => words.All(w => e.Title.Contains(w, StringComparison.OrdinalIgnoreCase) || e.Search.Contains(w, StringComparison.OrdinalIgnoreCase)))
			.Where(e => (producing is null || IsScriptObject(producing)) && (expecting is null || e.Result is not null && m_checker.Check(e.Result, expecting).Kind != Compat.Blocked));
		var selves = SelfEntries().Where(e => producing is null && !statementsOnly && words.All(w => e.Title.Contains(w, StringComparison.OrdinalIgnoreCase) || e.Search.Contains(w, StringComparison.OrdinalIgnoreCase)));
		return selves.Concat(own).Concat(functions).Concat(Catalog.Search(text, producing, expecting, statementsOnly)).ToList();
	}

	public static InputPin? PrimaryInput(GraphNode node) {
		switch (node) {
			case LocalDecl l: return NodeShape.CallOf(l) is { } call ? PrimaryInput(call) : l.Values.FirstOrDefault();
			case ReturnStatement r: return r.Values.FirstOrDefault() ?? r.AddValue();
			case IfStatement i: return i.Clauses.FirstOrDefault()?.Condition;
			case WhileStatement w: return w.Condition;
			case RepeatStatement rp: return rp.Condition;
			case NumericFor f: return f.Start;
			case GenericFor g: return g.Exprs.FirstOrDefault();
			case CallStatement s when s.CallNode is { } c: return PrimaryInput(c);
			case CallNode c: return c.Style == CallStyle.Method ? c.Target : c.Args.FirstOrDefault() ?? c.Target;
			case BinaryOp b: return b.Left;
			case UnaryOp u: return u.Operand;
			case MemberGet m: return m.Object;
			case IndexGet x: return x.Object;
			case ParenNode p: return p.Inner;
			case TableNode t: return t.Fields.FirstOrDefault()?.Value ?? t.AddPositional().Value;
			default: return node.Inputs.FirstOrDefault();
		}
	}

	public InsertPoint InsertionFor(OutputPin? from, Vec at) {
		if (from?.Owner.OwnerStatement is { Parent: { } block } statement) return new InsertPoint(block, block.IndexOf(statement) + 1);
		return EndOfModule();
	}

	public InsertPoint InsertionNear(Vec at) {
		Statement? best = null;
		var bestDistance = double.MaxValue;
		foreach (var (node, _) in Layout.Keys.Ordered) {
			if (node is not Statement s || s.Parent is null || s is FunctionDecl or SequenceStatement || !Layout.IsPlaced(s)) continue;
			var b = Layout.Bounds(s);
			var dx = Math.Max(Math.Max(b.X - at.X, at.X - b.Right), 0);
			var dy = Math.Max(Math.Max(b.Y - at.Y, at.Y - b.Bottom), 0);
			var d = dx * dx + dy * dy;
			if (d < bestDistance) {
				bestDistance = d;
				best = s;
			}
		}
		if (best is null || bestDistance > 80 * 80) return EndOfModule();
		var bounds = Layout.Bounds(best);
		var index = best.Parent!.IndexOf(best);
		var place = at.X >= bounds.X + bounds.W / 2 ? index + 1 : index;
		// lua forbids statements after a return or break
		while (place > 0 && place <= best.Parent.Statements.Count && best.Parent.Statements[place - 1] is ReturnStatement or BreakStatement or GotoStatement) place--;
		return new InsertPoint(best.Parent, place);
	}

	public InsertPoint FloatingPlace(Vec at) {
		Block? body = null;
		foreach (var (shown, host) in Layout.Embedded)
			if (Layout.IsPlaced(host) && Layout.Bounds(host) is var r && at.X >= r.X && at.X <= r.Right && at.Y >= r.Y && at.Y <= r.Bottom) body = shown;
		if (body is null)
			foreach (var region in Layout.Keys.Regions)
				if (!region.IsModule && region.Header is FunctionDecl && !Layout.Embedded.ContainsKey(region.Body) && Layout.RegionExtent(region) is { } e && at.X >= e.X - 12 && at.X <= e.Right + 12 && at.Y >= e.Y - 30 && at.Y <= e.Bottom + 12)
					body = region.Body;
		if (body is null) return EndOfModule();
		var count = body.Statements.Count;
		return new InsertPoint(body, count > 0 && body.Statements[^1] is ReturnStatement ? count - 1 : count);
	}

	public InsertPoint EndOfModule() {
		var module = Graph.Module;
		var count = module.Statements.Count;
		return new InsertPoint(module, count > 0 && module.Statements[^1] is ReturnStatement ? count - 1 : count);
	}

	public bool Delete(IEnumerable<GraphNode> nodes) {
		var list = nodes.Select(InlineRules.Drawn).Distinct().ToList();
		if (list.Count == 0) return false;
		bool Removes(GraphNode target) => list.Contains(target) ||
			(target as Statement ?? target.OwnerStatement) is { } statement &&
			list.OfType<Statement>().Any(owner => owner == statement || Contains(owner, statement));
		if (Layout.Embedded.Values.Any(host => Removes(host) || NodeShape.LocalCalleeOf(host) is { } callee && Removes(callee))) {
			Message = "close the active POE frame first";
			Raise();
			return false;
		}
		string? refused = null;
		var ok = Edit(() => {
			var statements = list.OfType<Statement>().Where(s => s.Parent is not null).ToList();
			foreach (var s in statements.Where(s => !statements.Any(o => o != s && Contains(o, s))).ToList()) {
				try {
					Graph.Remove(s);
				} catch (InvalidOperationException e) {
					refused ??= e.Message;
				}
			}
			foreach (var e in list.OfType<ExprNode>()) {
				if (Layout.Loose.Contains(e)) {
					foreach (var consumer in e.Result.Consumers.ToList()) Graph.Disconnect(consumer);
					Layout.Loose.Remove(e);
					Layout.Local.Remove(e);
					Graph.Unregister(e);
				} else {
					foreach (var consumer in e.Result.Consumers.ToList()) Graph.Disconnect(consumer);
				}
			}
			if (refused is not null) throw new InvalidOperationException(refused);
		});
		if (!ok) return false;
		Selection.Clear();
		SelectionChanged?.Invoke();
		Raise();
		return true;
	}

	private static Statement? ParentStatement(Statement s) => s.Parent?.Owner switch {
		Statement p => p,
		FunctionEntry e => e.Owner as Statement ?? e.Owner.OwnerStatement,
		_ => null
	};

	private static bool Contains(Statement outer, Statement inner) {
		for (var p = ParentStatement(inner); p is not null; p = ParentStatement(p))
			if (p == outer) return true;
		return false;
	}

	private string? m_lastCopy;

	public string CopyText() => m_lastCopy = Fragments.Copy(SelectedStatements());

	private List<Statement> SelectedStatements() {
		var chosen = Selection.OfType<Statement>().Where(s => s.Parent is not null && s is not FunctionDecl).ToList();
		var top = chosen.Where(s => !chosen.Any(o => o != s && Contains(o, s))).ToList();
		var ordered = new List<Statement>();
		foreach (var (node, _) in Layout.Keys.Ordered)
			if (node is Statement s && top.Contains(s)) ordered.Add(s);
		return ordered;
	}

	public bool Paste(string luaText, Vec at) {
		var point = InsertionNear(at);
		IReadOnlyList<Statement> created = [];
		var ok = Edit(() => {
			var before = Graph.Nodes.Count;
			var result = Fragments.Paste(Graph, luaText, point.Block, point.Index);
			if (result.Errors.Count > 0) throw new InvalidOperationException(result.Errors[0].Message);
			created = result.Statements;
			// pasted nodes must not keep wires to unselected nodes
			if (luaText == m_lastCopy) DropOuterWires(before);
		});
		if (!ok) return false;
		Layout.Refresh();
		Selection.Clear();
		foreach (var s in created) Selection.Add(s);
		if (created.Count > 0 && Layout.IsPlaced(created[0])) MoveGroup(created[0], at - Layout.Position(created[0]));
		LayoutEngine.Separate(Layout, created);
		Raise();
		return true;
	}

	private void DropOuterWires(int nodesBefore) {
		var fresh = Graph.Nodes.Skip(nodesBefore).ToHashSet();
		foreach (var node in fresh)
			foreach (var pin in node.Inputs.ToList())
				if (pin.Source is { } source && !fresh.Contains(source.Owner) && !InlineRules.IsImplicitObject(pin)) Graph.Disconnect(pin);
	}

	public bool Duplicate() {
		var statements = SelectedStatements();
		if (statements.Count == 0) {
			Message = "select statements to duplicate";
			Raise();
			return false;
		}
		var last = statements[^1];
		var block = last.Parent!;
		var text = Fragments.Copy(statements);
		var offset = Layout.Position(statements[0]);
		IReadOnlyList<Statement> created = [];
		var ok = Edit(() => {
			var before = Graph.Nodes.Count;
			var result = Fragments.Paste(Graph, text, block, block.IndexOf(last) + 1);
			if (result.Errors.Count > 0) throw new InvalidOperationException(result.Errors[0].Message);
			created = result.Statements;
			DropOuterWires(before);
		});
		if (!ok) return false;
		Layout.Refresh();
		Selection.Clear();
		Selection.AddRange(created);
		_ = offset;
		Raise();
		return true;
	}

	public void Move(IEnumerable<GraphNode> nodes, Vec delta) {
		var list = nodes.Select(InlineRules.Drawn).Distinct().ToList();
		var moving = list.Where(node => Layout.IsPlaced(node) && !list.Any(host => host != node && Layout.IsInsideFrameOf(node, host))).ToList();
		foreach (var node in moving) Layout.AnchorFrame(node);
		foreach (var node in moving) {
			var constrained = Layout.ConstrainFrameMove(node, delta);
			delta = new Vec(Math.Max(delta.X, constrained.X), Math.Max(delta.Y, constrained.Y));
		}
		foreach (var n in moving) {
			Layout.Local[n] += delta;
			Layout.PinnedNodes.Add(n);
		}
		foreach (var (body, host) in Layout.Embedded.ToList())
			if (list.Contains(host))
				foreach (var waiting in Layout.Loose.OfType<Statement>().Where(x => x.IsFloating && x.Parent == body).ToList()) Layout.Local[waiting] += delta;
		LayoutEngine.FollowHosts(Layout);
		RefitGroups(list);
		Raise();
	}

	public void FinishMove(IEnumerable<GraphNode> nodes) {
		var selected = nodes.Select(InlineRules.Drawn).Distinct().ToList();
		var list = selected.Where(node => !selected.Any(host => host != node && Layout.IsInsideFrameOf(node, host))).ToList();
		LayoutEngine.Separate(Layout, list, includeInputs: false);
		RefitGroups(list);
		CommitLayout();
	}

	public static Rect GroupRectAround(Rect nodes) => new(nodes.X - 16, nodes.Y - 36, nodes.W + 32, nodes.H + 52);

	private void RefitGroups(IReadOnlyCollection<GraphNode> moved) {
		foreach (var group in Layout.Groups) {
			if (!group.Members.Any(moved.Contains) || group.Members.All(moved.Contains)) continue;
			var bounds = group.Members.Where(Layout.IsPlaced).Select(Layout.Bounds).ToList();
			if (bounds.Count > 0) group.Rect = GroupRectAround(bounds.Aggregate(Rect.Union));
		}
	}

	public void FinishResizeGroup(LayoutGroup group) {
		var inside = group.Members.Where(m => Layout.IsPlaced(m) && Contains(group.Rect, Layout.Bounds(m))).ToList();
		if (inside.Count == 0) Message = "a comment box keeps at least one node";
		else {
			group.Members.Clear();
			group.Members.AddRange(inside);
		}
		CommitLayout();
	}

	private static bool Contains(Rect outer, Rect inner) => inner.X >= outer.X - 2 && inner.Y >= outer.Y - 2 && inner.Right <= outer.Right + 2 && inner.Bottom <= outer.Bottom + 2;

	public void FinishRegionMove() {
		LayoutEngine.PackRegions(Layout);
		CommitLayout();
	}

	public void CommitLayout() => Edit(() => { }, structural: false);

	private void MoveGroup(GraphNode node, Vec delta) {
		if (delta == default) return;
		if (Layout.Loose.Contains(node)) {
			Layout.Local[node] += delta;
			return;
		}
		Layout.Local[node] = Layout.Local.GetValueOrDefault(node) + delta;
		if (node is not Statement) return;
		var key = Layout.Keys.KeyOf(node);
		if (key is null) return;
		foreach (var (other, otherKey) in Layout.Keys.Ordered) {
			if (!otherKey.StartsWith(key + "#", StringComparison.Ordinal) || !Layout.Local.ContainsKey(other)) continue;
			Layout.Local[other] += delta;
		}
	}

	public void MoveRegion(RegionInfo region, Vec delta) {
		Layout.MoveRegion(region, delta);
		Layout.PinnedRegions.Add(region.Body);
		Raise();
	}

	public bool Tidy(RegionInfo? region, bool includePinned = false) => Edit(() => {
		var targets = region is null ? Layout.Keys.Regions : [region];
		var selection = region is null && Selection.Count > 0 ? Selection : null;
		foreach (var r in targets) {
			var chosen = selection?.Where(n => Layout.Keys.RegionOf(n) == r).ToList();
			if (selection is not null && chosen!.Count == 0) continue;
			Arrange.Tidy(Layout, r, chosen, includePinned);
		}
		LayoutEngine.PackRegions(Layout);
	}, structural: false);

	public void StraightenSelection(IReadOnlyList<(Slot From, Slot To)> links) {
		if (links.Count == 0 || !Arrange.StraightenSelection(Layout, links)) return;
		RefitGroups(Selection);
		CommitLayout();
	}

	public bool Straighten(OutputPin from, InputPin to) => Edit(() => {
		if (!Arrange.Straighten(Layout, from, to)) throw new InvalidOperationException("that would put the node on top of another one");
		var target = InlineRules.Drawn(to.Owner);
		Layout.PinnedNodes.Add(target);
	}, structural: false);

	public bool Align(AlignEdge edge) => Edit(() => {
		Arrange.Align(Layout, Selection, edge);
		foreach (var n in Selection) Layout.PinnedNodes.Add(n);
		LayoutEngine.FollowHosts(Layout);
		if (Layout.Overlaps().Any()) throw new InvalidOperationException("that would put nodes on top of each other");
	}, structural: false);

	public bool Distribute(bool horizontal) => Edit(() => {
		Arrange.Distribute(Layout, Selection, horizontal);
		foreach (var n in Selection) Layout.PinnedNodes.Add(n);
		LayoutEngine.FollowHosts(Layout);
		if (Layout.Overlaps().Any()) throw new InvalidOperationException("that would put nodes on top of each other");
	}, structural: false);

	public void MoveProxy(InputPin pin, Vec delta) {
		Layout.ProxyOffsets[pin] = Layout.ProxyOffsets.GetValueOrDefault(pin) + delta;
		Raise();
	}

	private Vec RerouteBase(object wire) => wire is InputPin pin ? Layout.Position(InlineRules.Drawn(pin.Owner)) : Layout.Position((GraphNode)wire);

	private List<Vec>? PointsOf(object wire, bool create) {
		if (wire is InputPin p) {
			if (Layout.Reroutes.TryGetValue(p, out var list)) return list;
			return create ? Layout.Reroutes[p] = [] : null;
		}
		var statement = (Statement)wire;
		if (Layout.ExecReroutes.TryGetValue(statement, out var exec)) return exec;
		return create ? Layout.ExecReroutes[statement] = [] : null;
	}

	private void DropPoints(object wire) {
		if (wire is InputPin pin) Layout.Reroutes.Remove(pin);
		else Layout.ExecReroutes.Remove((Statement)wire);
	}

	public int AddReroute(object wire, Vec world, int index = -1) {
		var list = PointsOf(wire, true)!;
		if (index < 0 || index > list.Count) index = list.Count;
		list.Insert(index, world - RerouteBase(wire));
		CommitLayout();
		return index;
	}

	public void MoveReroute(object wire, int index, Vec world) {
		if (PointsOf(wire, false) is not { } list || index < 0 || index >= list.Count) return;
		list[index] = world - RerouteBase(wire);
		Raise();
	}

	public bool RemoveReroute(object wire, int index) => Edit(() => {
		if (PointsOf(wire, false) is not { } list || index < 0 || index >= list.Count) return;
		list.RemoveAt(index);
		if (list.Count == 0) DropPoints(wire);
	}, structural: false);

	public LayoutGroup? AddCommentBox(string title = "Comment") {
		var members = Selection.Where(n => Layout.IsPlaced(n) && !InlineRules.IsMerged(n)).ToList();
		if (members.Count == 0) return null;
		var group = new LayoutGroup { Title = title, Rect = GroupRectAround(members.Select(Layout.Bounds).Aggregate(Rect.Union)) };
		group.Members.AddRange(members);
		Edit(() => Layout.Groups.Add(group), structural: false);
		return group;
	}

	public bool CanOpen(GraphNode node) => node.IsExpanded || (NodeShape.LocalCalleeOf(node) is not null && Layout.HostBody(node) is { } body && !Layout.Embedded.ContainsKey(body));

	public bool ToggleExpanded(GraphNode node) {
		if (!node.IsExpanded) {
			string? problem = null;
			if (NodeShape.LocalCalleeOf(node) is not { } callee || Layout.Keys.Regions.FirstOrDefault(r => r.Header == callee) is not { } region) problem = "only local function calls can open in POE";
			else if (Layout.HostBody(node) == region.Body) problem = "a function cannot be opened inside itself";
			else if (!CanOpen(node)) problem = "a function cannot be opened inside another opened function; close that one first";
			if (problem is not null) {
				Message = problem;
				Raise();
				return false;
			}
		}
		return ToggleCore(node);
	}

	private bool ToggleCore(GraphNode node) => Edit(() => {
		if (node.IsExpanded) {
			node.IsExpanded = false;
			foreach (var body in Layout.Embedded.Where(e => e.Value == node).Select(e => e.Key).ToList()) {
				Layout.Embedded.Remove(body);
				Layout.FrameCorners.Remove(body);
			}
		} else {
			var callee = NodeShape.LocalCalleeOf(node) ?? throw new InvalidOperationException("only local function calls can open in POE");
			var region = Layout.Keys.Regions.FirstOrDefault(r => r.Header == callee) ?? throw new InvalidOperationException("that function has no graph here");
			if (Layout.HostBody(node) == region.Body) throw new InvalidOperationException("a function cannot be opened inside itself");
			if (!CanOpen(node)) throw new InvalidOperationException("a function cannot be opened inside another opened function; close that one first");
			if (Layout.Embedded.Values.Any(host => Layout.HostBody(host) == region.Body))
				throw new InvalidOperationException("close the POE frame inside this function first");
			// the same function cannot be edited in two frames
			if (Layout.Embedded.TryGetValue(region.Body, out var previous)) previous.IsExpanded = false;
			node.IsExpanded = true;
			Layout.Embedded[region.Body] = node;
			Layout.CollapsedRegions.Remove(region.Body);
		}
		for (var own = Layout.Keys.RegionOf(node); own is not null; own = Layout.Embedded.TryGetValue(own.Body, out var outer) ? Layout.Keys.RegionOf(outer) : null)
			Arrange.Tidy(Layout, own, null, false);
		LayoutEngine.PackRegions(Layout);
	}, structural: false);

	public bool RemoveCommentBox(LayoutGroup group) => Edit(() => Layout.Groups.Remove(group), structural: false);

	public bool GroupSelectionAsSequence() {
		var statements = SelectedStatements();
		if (statements.Count < 2 || statements.Any(s => s.Parent != statements[0].Parent)) {
			Message = "select two or more statements in the same block";
			Raise();
			return false;
		}
		var block = statements[0].Parent!;
		var starts = statements.Select(block.IndexOf).OrderBy(i => i).ToList();
		return Edit(() => Graph.GroupAsSequence(block, starts));
	}

	private void RetidyFrameOf(GraphNode? node) {
		if (node is null || node is Statement { IsFloating: true } || Layout.Keys.RegionOf(InlineRules.Drawn(node)) is not { } region || !Layout.Embedded.ContainsKey(region.Body)) return;
		Arrange.Tidy(Layout, region, null, includePinned: true);
		LayoutEngine.FollowHosts(Layout);
		m_current = m_current with { Meta = LayoutPersistence.Capture(Layout) };
		Raise();
	}

	public bool ConnectExec(Slot from, Statement to) {

		var floatingTarget = to.IsFloating ? to : null;
		var floatingSource = from.Node is Statement { IsFloating: true } fs ? fs : null;
		var joined = floatingTarget ?? floatingSource;
		var at = joined is null ? default : Layout.Position(joined);
		var ok = Edit(() => {
			if (from.Kind != SlotKind.ExecOut) throw new InvalidOperationException("drag from an execution output");
			if (floatingSource is not null) {
				if (from.Child is not null) throw new InvalidOperationException("wire this node into the script first, then its bodies");
				if (floatingTarget is not null) throw new InvalidOperationException("wire one of them into the script first");
				if (to.Parent is not { } block) throw new InvalidOperationException("that statement is not in the script");
				BringLocalCalleeIntoScope(floatingSource, block);
				floatingSource.IsFloating = false;
				Graph.Move(floatingSource, block, block.IndexOf(to));
				return;
			}
			if (floatingTarget is not null) floatingTarget.IsFloating = false;
			if (from.Child is { } child) {
				BringLocalCalleeIntoScope(to, child);
				Graph.Move(to, child, 0);
				return;
			}
			if (from.Node is not Statement source || source.Parent is null) throw new InvalidOperationException("that output does not lead anywhere");
			if (source == to) throw new InvalidOperationException("a statement cannot follow itself");
			BringLocalCalleeIntoScope(to, source.Parent);
			Graph.Move(to, source.Parent, source.Parent.IndexOf(source) + 1);
		});
		if (ok && joined is not null) {
			AdoptLoose(joined, at);
			m_current = m_current with { Meta = LayoutPersistence.Capture(Layout) };
			Raise();
		}
		if (ok) RetidyFrameOf(joined ?? to);
		return ok;
	}

	private void BringLocalCalleeIntoScope(Statement call, Block destination) {
		if (NodeShape.LocalCalleeOf(call) is not { Parent: { } module } local || module != Graph.Module) return;
		var owner = destination.Owner is FunctionEntry entry ? entry.Owner as Statement ?? entry.Owner.OwnerStatement : destination.Owner as Statement;
		while (owner?.Parent is not null && owner.Parent != module) owner = ParentStatement(owner);
		if (owner is not FunctionDecl method || ScriptInfo.Find(Graph)?.Methods.Contains(method) != true) return;
		if (module.IndexOf(local) > module.IndexOf(method)) Graph.Move(local, module, module.IndexOf(method));
	}

	public bool AddFunction(string name) {
		var table = ScriptInfo.Find(Graph)?.Table.Name ?? "M";
		return AddText($"function {table}:{name}()\nend\n");
	}

	public bool GroupSelectionAsFunction() {
		var chosen = SelectedStatements();
		if (chosen.Count == 0) {
			Message = "select statements to move into a function";
			Raise();
			return false;
		}
		var block = chosen[0].Parent!;
		var first = block.IndexOf(chosen[0]);
		for (var i = 0; i < chosen.Count; i++)
			if (chosen[i].Parent != block || block.IndexOf(chosen[i]) != first + i) {
				Message = "the statements must be next to each other to become a function";
				Raise();
				return false;
			}

		bool Inside(GraphNode n) => n is Statement s ? chosen.Any(c => c == s || Contains(c, s)) : n.OwnerStatement is { } o && chosen.Any(c => c == o || Contains(c, o));
		bool ModuleLevel(Binding b) => b.Declarer is LocalDecl d && d.Parent == Graph.Module;
		var parameters = new List<string>();
		void Need(Binding binding) {
			if (binding.Kind != BindingKind.Self && !ModuleLevel(binding) && !Inside(binding.Declarer) && !parameters.Contains(binding.Name)) parameters.Add(binding.Name);
		}
		var inside = Graph.Nodes.Where(Inside).ToList();
		foreach (var node in inside) {
			if (node is VariableGet get) Need(get.Binding);
			foreach (var pin in node.Inputs)
				if (pin.Source is { Binding: { } binding } && !Inside(pin.Source.Owner) && !InlineRules.IsImplicitObject(pin)) Need(binding);
		}
		foreach (var declared in inside.OfType<LocalDecl>()) {
			var used = declared.Outputs.Any(o => o.Consumers.Any(c => !Inside(c.Owner)))
				|| Graph.Nodes.OfType<VariableGet>().Any(v => !Inside(v) && v.Binding.Declarer == declared);
			if (used) {
				Message = "something the statements declare is used after them, so they cannot move into a function";
				Raise();
				return false;
			}
		}

		var table = ScriptInfo.Find(Graph)?.Table.Name ?? "M";
		var taken = Graph.Variables.Select(v => v.Name).ToHashSet();
		var name = "extracted";
		for (var n = 2; taken.Contains(name); n++) name = "extracted" + n;
		var arguments = string.Join(", ", parameters);
		var functionText = $"function {table}:{name}({arguments})\n{Fragments.Copy(chosen)}end\n";
		var owner = ObjectSource(block)?.Binding?.Name ?? table;
		var callText = $"{owner}:{name}({arguments})\n";
		var ok = Edit(() => {
			var end = EndOfModule();
			var made = Fragments.Paste(Graph, functionText, end.Block, end.Index);
			if (made.Errors.Count > 0) throw new InvalidOperationException(made.Errors[0].Message);
			var call = Fragments.Paste(Graph, callText, block, first);
			if (call.Errors.Count > 0) throw new InvalidOperationException(call.Errors[0].Message);
			foreach (var statement in chosen) Graph.Remove(statement);
		});
		if (ok) Message = null;
		return ok;
	}

	public bool AddModuleLocal(string name)
 => AddText($"local {name} = nil\n");

	public bool AddField(string name) {
		var script = ScriptInfo.Find(Graph);
		if (script is null) {
			Message = "this script has no table to add a field to";
			Raise();
			return false;
		}
		var index = script.Declaration.Parent!.IndexOf(script.Declaration) + 1;
		return Edit(() => {
			var result = Fragments.Paste(Graph, $"{script.Table.Name}.{name} = nil\n", script.Declaration.Parent!, index);
			if (result.Errors.Count > 0) throw new InvalidOperationException(result.Errors[0].Message);
		});
	}

	private bool AddText(string text) {
		var point = EndOfModule();
		return Edit(() => {
			var result = Fragments.Paste(Graph, text, point.Block, point.Index);
			if (result.Errors.Count > 0) throw new InvalidOperationException(result.Errors[0].Message);
		});
	}

	public bool Apply(Action<LuaGraph> change) => Edit(() => change(Graph));

	public bool Rename(Binding binding, string newName) => Edit(() => Graph.Rename(binding, newName));

	public bool CanSwitchCallStyle(CallNode call) => call.Style == CallStyle.Method
		? call.Method is not null
		: call.Target.Source is { Binding: null, Owner: MemberGet member } && call.Args.Count > 0 && call.Args[0].Source is { } first && member.Object.Source == first;

	public bool AddCallArgument(CallNode call) => Edit(() => call.AddArg("arg" + (call.Args.Count + 1)));

	public bool RemoveLastCallArgument(CallNode call) => Edit(() => {
		if (call.Args.Count == 0) return;
		var last = call.Args[^1];
		var source = last.Source;
		call.RemoveArgAt(call.Args.Count - 1);
		_ = source;
	});

	public bool SetCallStyle(CallNode call, CallStyle style) => Edit(() => Graph.SetCallStyle(call, style));

	public bool SetComment(Statement statement, string? text) => Edit(() => Graph.SetStatementComment(statement, text));
}
