using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Graph;

internal sealed class Typer {
	private sealed class Env(Env? parent) {
		private readonly Dictionary<Binding, LuaType> m_types = [];

		public Env? Parent { get; } = parent;

		public LuaType? Get(Binding binding) {
			for (var env = this; env is not null; env = env.Parent)
				if (env.m_types.TryGetValue(binding, out var type)) return type;
			return null;
		}

		public void Set(Binding binding, LuaType type) => m_types[binding] = type;

		public void CopyTo(Env target, Env? stop) {
			var seen = new HashSet<Binding>();
			for (var env = this; env is not null && env != stop; env = env.Parent)
				foreach (var (binding, type) in env.m_types)
					if (seen.Add(binding)) target.Set(binding, type);
		}
	}

	private readonly LuaGraph m_graph;
	private readonly TypeUniverse m_universe;
	private readonly TypeChecker m_checker;
	private readonly CallResolver m_resolver;
	private readonly ScriptInfo? m_script;
	private readonly List<GraphDiagnostic> m_diagnostics = [];
	private readonly Dictionary<Binding, MemberDecl> m_functions = [];
	private readonly Dictionary<Binding, int> m_returnCounts = [];
	private Env m_env = new(null);
	private IReadOnlyList<LuaType>? m_returns;

	private Typer(LuaGraph graph, TypeUniverse universe, ScriptInfo? script) {
		m_graph = graph;
		m_universe = universe;
		m_script = script;
		m_checker = new TypeChecker(universe);
		m_resolver = new CallResolver(m_checker, universe);
	}

	public static void Run(LuaGraph graph) {
		var first = new Typer(graph, graph.Universe, null);
		first.WalkBlock(graph.Module, new Env(null));
		var script = ScriptInfo.Find(graph);
		var universe = graph.Universe;
		if (script is not null) {
			var fieldTypes = new Dictionary<string, LuaType>();
			foreach (var field in script.Fields) fieldTypes[field.Name] = FieldTypeFrom(field.Value.ActualType);
			universe = universe.Extend(script.ToDeclarations(fieldTypes));
		}
		var second = new Typer(graph, universe, script);
		second.WalkBlock(graph.Module, new Env(null));
		foreach (var loose in graph.Nodes.OfType<ExprNode>().Where(n => n.OwnerStatement is null).ToList()) second.TypeOutput(loose.Result);
		second.TypeTableTargets(script);
		graph.SetDiagnostics(second.m_diagnostics);
	}

	internal static LuaType FieldTypeFrom(LuaType value) => value switch {
		NamedType { Name: "TypeMarker", Args: [var target] } => target,
		_ when value == LuaType.Nil || value == LuaType.Unknown => LuaType.Any,
		_ => value
	};

	private static LuaType Widen(LuaType type) => type == LuaType.Nil || type == LuaType.Unknown ? LuaType.Any : type;

	private static bool IsAny(LuaType type) => type is PrimitiveType { Kind: PrimitiveKind.Any or PrimitiveKind.Unknown };

	private static bool HasNil(LuaType type) => type == LuaType.Nil || (type is UnionType u && u.Members.Contains(LuaType.Nil));

	private LuaType NonNil(LuaType type) => HasNil(type) ? m_checker.NarrowNonNil(type) : type;

	private void Report(DiagnosticSeverity severity, string message, GraphNode node, InputPin? pin) => m_diagnostics.Add(new GraphDiagnostic(severity, message, node, pin));

	private void Check(InputPin pin, LuaType expected) {
		pin.Expected = expected;
		var result = m_checker.Check(pin.ActualType, expected);
		if (result.Kind == Compat.Allowed) return;
		Report(result.Kind == Compat.Warning ? DiagnosticSeverity.Warning : DiagnosticSeverity.Error, result.Reason ?? $"{pin.ActualType} does not fit {expected}", pin.Owner, pin);
	}

	private void WarnIfNullable(InputPin pin, LuaType type, GraphNode node) {
		if (type is UnionType union && union.Members.Contains(LuaType.Nil) && !union.Members.Any(IsAny))
			Report(DiagnosticSeverity.Warning, $"{type} may be nil here", node, pin);
	}

	private void WalkBlock(Block block, Env env) {
		var saved = m_env;
		m_env = env;
		foreach (var statement in block.Statements) WalkStatement(statement);
		m_env = saved;
	}

	private void WalkStatement(Statement statement) {
		switch (statement) {
			case LocalDecl local:
				WalkLocal(local);
				break;
			case AssignStatement assign:
				WalkAssign(assign);
				break;
			case CallStatement call:
				Value(call.Call);
				break;
			case IfStatement @if:
				WalkIf(@if);
				break;
			case WhileStatement @while: {
				Value(@while.Condition);
				var body = new Env(m_env);
				Narrow(@while.Condition, true, body);
				WalkBlock(@while.Body, body);
				break;
			}
			case RepeatStatement repeat: {
				var body = new Env(m_env);
				WalkBlock(repeat.Body, body);
				var saved = m_env;
				m_env = body;
				Value(repeat.Condition);
				m_env = saved;
				break;
			}
			case NumericFor numeric:
				WalkNumericFor(numeric);
				break;
			case GenericFor generic:
				WalkGenericFor(generic);
				break;
			case DoStatement @do:
				WalkBlock(@do.Body, new Env(m_env));
				break;
			case FunctionDecl function:
				WalkFunctionDecl(function);
				break;
			case ReturnStatement @return:
				for (var i = 0; i < @return.Values.Count; i++) {
					Value(@return.Values[i]);
					if (m_returns is not null && i < m_returns.Count) Check(@return.Values[i], m_returns[i]);
				}
				break;
			case CastStatement cast:
				ApplyCast(cast);
				break;
			case SequenceStatement sequence:
				foreach (var branch in sequence.Branches) WalkBlock(branch, m_env);
				break;
		}
	}

	private void WalkLocal(LocalDecl local) {
		var annotations = ScriptInfo.AnnotationsOf(local);
		var declared = annotations.Tags.OfType<TypeTag>().FirstOrDefault()?.Type;
		var values = local.Values.Select(Value).ToList();
		for (var i = 0; i < local.Names.Count; i++) {
			var binding = local.Names[i];
			LuaType? valueType = null;
			if (i < local.Values.Count && (i < local.Values.Count - 1 || local.Names.Count <= local.Values.Count || !IsMultiValue(local.Values[^1]))) {
				valueType = values[i];
			} else if (local.Values.Count > 0 && local.Values[^1].Source is { Owner: CallNode multi }) {
				var index = i - (local.Values.Count - 1);
				valueType = index < multi.Outputs.Count ? multi.Outputs[index].Type : LuaType.Any;
			}
			binding.IsDeclared = i == 0 && declared is not null;
			binding.Type = (i == 0 ? declared : null) ?? Widen(valueType ?? LuaType.Any);
			if (m_script is not null && binding == m_script.Table) binding.Type = new NamedType(m_script.ClassName, []);
			binding.Output.Type = binding.Type;
		}
	}

	private static bool IsMultiValue(InputPin pin) => pin.Source is { Owner: CallNode or VarargNode, Index: 0, Binding: null };

	private void WalkAssign(AssignStatement assign) {
		var expected = new List<LuaType?>();
		foreach (var target in assign.Targets) {
			switch (target.Kind) {
				case TargetKind.Binding:
					expected.Add(target.Binding!.IsDeclared && !IsAny(target.Binding.Type) ? LuaType.Optional(target.Binding.Type) : null);
					break;
				case TargetKind.Member: {
					var objectType = Value(target.Object!);
					var member = LookupMember(objectType, target.Name!, target.Object!, assign);
					expected.Add(member is { Type: not FunctionType } && !IsAny(member.Type) && IsEnforced(objectType, target.Name!) ? LuaType.Optional(member.Type) : null);
					break;
				}
				case TargetKind.Index:
					Value(target.Object!);
					Value(target.Key!);
					expected.Add(null);
					break;
				default:
					expected.Add(null);
					break;
			}
		}
		for (var i = 0; i < assign.Values.Count; i++) {
			var pin = assign.Values[i];
			var actual = Value(pin);
			if (i >= expected.Count || expected[i] is not { } type) continue;
			if (actual is NamedType { Name: "TypeMarker", Args: [var target] } &&
				m_script?.Fields.Any(field => field.Value == pin && !field.Name.StartsWith('_')) == true &&
				m_checker.Check(target, type).Kind == Compat.Allowed) {
				pin.Expected = type;
				continue;
			}
			Check(pin, type);
		}
	}

	private bool IsEnforced(LuaType receiver, string field) {
		if (m_script is null || NonNil(receiver) is not NamedType named) return true;
		var declaringClass = m_universe.MembersOf(named.Name).FirstOrDefault(m => m.Decl.Name == field)?.DeclaringClass;
		if (declaringClass != m_script.ClassName) return true;
		return m_script.Fields.Any(f => f.Name == field && f.Annotations.Tags.OfType<TypeTag>().Any());
	}

	private void WalkIf(IfStatement statement) {
		var outer = m_env;
		var falseSoFar = new Env(outer);
		var allExit = true;
		foreach (var clause in statement.Clauses) {
			m_env = falseSoFar;
			Value(clause.Condition);
			var branch = new Env(falseSoFar);
			Narrow(clause.Condition, true, branch);
			WalkBlock(clause.Body, branch);
			allExit &= Exits(clause.Body);
			var next = new Env(falseSoFar);
			Narrow(clause.Condition, false, next);
			falseSoFar = next;
		}
		m_env = outer;
		if (statement.Else is not null) {
			WalkBlock(statement.Else, new Env(falseSoFar));
			return;
		}
		if (allExit) falseSoFar.CopyTo(outer, outer);
	}

	private static bool Exits(Block block) {
		var last = block.Statements.LastOrDefault(s => s is not CommentStatement and not CastStatement);
		return last switch {
			ReturnStatement or BreakStatement or GotoStatement => true,
			CallStatement { CallNode: { Style: CallStyle.Function } call } => call.Target.Source?.Owner is GlobalGet { Name: "error" },
			_ => false,
		};
	}

	private void WalkNumericFor(NumericFor numeric) {
		Value(numeric.Start);
		Value(numeric.Limit);
		Check(numeric.Start, LuaType.Number);
		Check(numeric.Limit, LuaType.Number);
		var allIntegers = numeric.Start.ActualType == LuaType.Integer;
		if (!numeric.Step.IsEmpty) {
			Value(numeric.Step);
			Check(numeric.Step, LuaType.Number);
			allIntegers &= numeric.Step.ActualType == LuaType.Integer;
		}
		numeric.Var.Type = allIntegers ? LuaType.Integer : LuaType.Number;
		numeric.Var.Output.Type = numeric.Var.Type;
		WalkBlock(numeric.Body, new Env(m_env));
	}

	private void WalkGenericFor(GenericFor generic) {
		foreach (var expression in generic.Exprs) Value(expression);
		var types = new List<LuaType> { LuaType.Any, LuaType.Any };
		if (generic.Exprs.Count > 0 && generic.Exprs[0].Source is { Owner: CallNode { Style: CallStyle.Function, Args.Count: >= 1 } call }
		    && call.Target.Source?.Owner is GlobalGet { Name: "ipairs" or "pairs" } iterator) {
			var collection = NonNil(call.Args[0].ActualType);
			switch (collection) {
				case ArrayType array:
					types = [LuaType.Integer, array.Element];
					break;
				case TableType table when iterator.Name == "pairs":
					types = [table.Key, table.Value];
					break;
			}
		}
		for (var i = 0; i < generic.Vars.Count; i++) {
			generic.Vars[i].Type = i < types.Count ? types[i] : LuaType.Any;
			generic.Vars[i].Output.Type = generic.Vars[i].Type;
		}
		WalkBlock(generic.Body, new Env(m_env));
	}

	private void WalkFunctionDecl(FunctionDecl function) {
		var annotations = ScriptInfo.AnnotationsOf(function);
		var signature = ScriptInfo.SignatureOf(function.Entry, annotations);
		if (function.Path.Count > 1) Value(function.Root);

		foreach (var parameter in function.Entry.Params) {
			var tag = annotations.Tags.OfType<ParamTag>().FirstOrDefault(t => t.Name == parameter.Name);
			parameter.IsDeclared = tag is not null;
			parameter.Type = tag is null ? LuaType.Any : (tag.Optional ? LuaType.Optional(tag.Type) : tag.Type);
			parameter.Output.Type = parameter.Type;
		}
		if (function.Entry.Self is { } self) {
			var inScript = m_script is not null && function.Path.Count == 2 && m_script.IsTable(function.Root);
			self.Type = inScript ? new NamedType(m_script!.ClassName, []) : LuaType.Any;
			self.Output.Type = self.Type;
		}
		if (function.FunctionBinding is { } binding) {
			// recursive calls need the signature before the body is typed
			binding.Type = signature;
			binding.Output.Type = signature;
			m_functions[binding] = ScriptInfo.MemberOf(function, annotations, binding.Name, false);
		}
		if (function.FunctionBinding is { } local && !annotations.Tags.OfType<ReturnTag>().Any())
			m_returnCounts[local] = ReturnSites(function.Entry.Body).Select(r => r.Values.Count).DefaultIfEmpty().Max();
		WalkFunctionBody(function.Entry.Body, signature.Returns);
	}

	private static IEnumerable<ReturnStatement> ReturnSites(Block block) {
		foreach (var statement in block.Statements) {
			if (statement is ReturnStatement ret) yield return ret;
			if (statement is FunctionDecl) continue;
			foreach (var child in statement.ChildBlocks)
				foreach (var nested in ReturnSites(child)) yield return nested;
		}
	}

	private void WalkFunctionBody(Block body, IReadOnlyList<LuaType>? returns) {
		var savedReturns = m_returns;
		m_returns = returns;
		WalkBlock(body, new Env(m_env));
		m_returns = savedReturns;
	}

	private void ApplyCast(CastStatement cast) {
		if (cast.Target is not { } binding || string.IsNullOrWhiteSpace(cast.TypeText)) return;
		var text = cast.TypeText.Trim();
		var mode = text[0] is '+' or '-' ? text[0] : ' ';
		if (mode != ' ') text = text[1..].Trim();
		if (!TypeExprParser.TryParsePrefix(text, 0, out var type, out _, out _)) return;
		var current = m_env.Get(binding) ?? binding.Type;
		m_env.Set(binding, mode switch {
			'+' => LuaType.Union(current, type),
			'-' => RemoveFrom(current, type),
			_ => type,
		});
	}

	private static LuaType RemoveFrom(LuaType current, LuaType removed) {
		if (current is not UnionType union) return current == removed ? LuaType.Unknown : current;
		var remaining = union.Members.Where(m => m != removed).ToArray();
		return remaining.Length == 0 ? LuaType.Unknown : LuaType.Union(remaining);
	}

	private static Binding? BindingOf(InputPin pin) => pin.Source?.Binding ?? (pin.Source?.Owner as VariableGet)?.Binding;

	private void Narrow(InputPin condition, bool truthy, Env env) {
		if (condition.Source is not { } source) return;
		if (BindingOf(condition) is { } binding) {
			if (truthy) env.Set(binding, NonNil(env.Get(binding) ?? binding.Type));
			return;
		}
		switch (source.Owner) {
			case UnaryOp { Op: UnaryKind.Not } not:
				Narrow(not.Operand, !truthy, env);
				break;
			case BinaryOp { Op: BinaryKind.And } both when truthy:
				Narrow(both.Left, true, env);
				Narrow(both.Right, true, env);
				break;
			case BinaryOp { Op: BinaryKind.Or } either when !truthy:
				Narrow(either.Left, false, env);
				Narrow(either.Right, false, env);
				break;
			case BinaryOp { Op: BinaryKind.Eq or BinaryKind.Ne } comparison: {
				var holds = truthy == (comparison.Op == BinaryKind.Eq);
				NarrowComparison(comparison.Left, comparison.Right, holds, env);
				NarrowComparison(comparison.Right, comparison.Left, holds, env);
				break;
			}
		}
	}

	private void NarrowComparison(InputPin subject, InputPin other, bool holds, Env env) {
		if (other.Constant is { Kind: ConstantKind.Nil } && BindingOf(subject) is { } nilChecked) {
			if (!holds) env.Set(nilChecked, NonNil(env.Get(nilChecked) ?? nilChecked.Type));
			return;
		}
		if (other.Constant is { Kind: ConstantKind.String } name && subject.Source?.Owner is CallNode { Style: CallStyle.Function, Args.Count: 1 } call
		    && call.Target.Source?.Owner is GlobalGet { Name: "type" } && BindingOf(call.Args[0]) is { } typed && holds) {
			env.Set(typed, m_checker.NarrowToTypeName(env.Get(typed) ?? typed.Type, name.Raw.Trim('"', '\'')));
		}
	}

	private LuaType Value(InputPin pin) {
		LuaType type;
		LuaType? narrowed = null;
		if (pin.Constant is { } constant) {
			type = constant.ValueType;
		} else if (pin.Source is { } source) {
			type = TypeOutput(source);
			if (BindingOf(pin) is { } binding) narrowed = m_env.Get(binding);
		} else {
			type = LuaType.Nil;
		}
		pin.ActualOverride = narrowed;
		return narrowed ?? type;
	}

	private LuaType TypeOutput(OutputPin output) {
		if (output.Binding is { } binding) return binding.Type;
		if (output.Owner is ExprNode node) TypeNode(node);
		return output.Type;
	}

	private LuaType GlobalType(string name) {
		if (m_universe.GlobalType(name) is { } declared) return declared;
		if (m_universe.Enums.FirstOrDefault(e => e.TableName == name) is { } table) return new NamedType(table.Name, []);
		return LuaType.Any;
	}

	private void TypeNode(ExprNode node) {
		switch (node) {
			case GlobalGet global:
				node.Result.Type = GlobalType(global.Name);
				break;
			case VariableGet get:
				node.Result.Type = get.Binding.Type;
				break;
			case ParenNode paren:
				node.Result.Type = Value(paren.Inner);
				break;
			case MemberGet member: {
				var objectType = Value(member.Object);
				var found = LookupMember(objectType, member.Name, member.Object, node);
				node.Result.Type = found is { IsMethod: true, Type: FunctionType method }
					? WithReceiver(method, NonNil(objectType)) : found?.Type ?? LuaType.Any;
				break;
			}
			case IndexGet index: {
				var objectType = NonNil(Value(index.Object));
				Value(index.Key);
				node.Result.Type = objectType switch {
					ArrayType array => array.Element,
					TableType table => table.Value,
					_ => LuaType.Any,
				};
				break;
			}
			case CallNode call:
				TypeCall(call);
				break;
			case BinaryOp binary:
				TypeBinary(binary);
				break;
			case UnaryOp unary:
				TypeUnary(unary);
				break;
			case TableNode table:
				TypeTable(table);
				break;
			case FunctionNode function:
				WalkClosure(function);
				break;
			default:
				node.Result.Type = LuaType.Any;
				break;
		}
	}

	private void WalkClosure(FunctionNode function) {
		foreach (var parameter in function.Entry.Params) {
			parameter.Type = LuaType.Any;
			parameter.Output.Type = LuaType.Any;
		}
		WalkFunctionBody(function.Entry.Body, null);
		function.Result.Type = new FunctionType(function.Entry.Params.Select(p => new Param(p.Name, LuaType.Any, true)).ToList(), [], null, LuaType.Any);
	}

	private void TypeTable(TableNode table) {
		var elements = new List<LuaType>();
		var positionalOnly = table.Fields.Count > 0;
		foreach (var field in table.Fields) {
			if (field.Key is not null) Value(field.Key);
			var type = Widen(Value(field.Value));
			if (field.Kind == TableFieldKind.Positional) elements.Add(type);
			else positionalOnly = false;
		}
		table.Result.Type = table.Flavor switch {
			TableFlavor.Array => new ArrayType(elements.Count == 0 ? LuaType.Any : LuaType.Union(elements.ToArray())),
			TableFlavor.Dictionary => DictionaryType(table),
			_ => positionalOnly ? new ArrayType(LuaType.Union(elements.ToArray())) : LuaType.Table
		};
	}

	private LuaType DictionaryType(TableNode table) {
		var keys = table.Fields.Where(f => f.Key is not null).Select(f => Widen(Value(f.Key!))).ToArray();
		var values = table.Fields.Select(f => Widen(Value(f.Value))).ToArray();
		return new TableType(keys.Length == 0 ? LuaType.Any : LuaType.Union(keys), values.Length == 0 ? LuaType.Any : LuaType.Union(values));
	}

	private static LuaType WithoutNil(LuaType type) => type is UnionType u && u.Members.Count(x => x != LuaType.Nil) == 1 ? u.Members.First(x => x != LuaType.Nil) : type;

	private static LuaType? StoredAs(InputPin consumer, ScriptInfo? script) {
		if (consumer.Expected is { } expected && WithoutNil(expected) is ArrayType or TableType) return WithoutNil(expected);
		if (consumer.Owner is Statement statement && ScriptInfo.AnnotationsOf(statement).Tags.OfType<TypeTag>().FirstOrDefault()?.Type is { } declared && WithoutNil(declared) is ArrayType or TableType) return WithoutNil(declared);
		if (script?.Fields.FirstOrDefault(f => f.Value == consumer)?.Annotations.Tags.OfType<TypeTag>().FirstOrDefault()?.Type is { } field && WithoutNil(field) is ArrayType or TableType) return WithoutNil(field);
		return null;
	}

	internal void TypeTableTargets(ScriptInfo? script) {
		var tables = m_graph.Nodes.OfType<TableNode>().ToList();
		foreach (var table in tables) table.TargetType = null;
		// nested tables need a second pass to inherit the outer type
		for (var pass = 0; pass < 2; pass++)
			foreach (var table in tables) {
				LuaType? target = null;
				foreach (var consumer in table.Result.Consumers) {
					target = StoredAs(consumer, script);
					if (target is null && consumer.Owner is TableNode outer && outer.TargetType is { } outerType && outer.Fields.FirstOrDefault(f => f.Value == consumer) is { } entry)
						target = outerType switch { ArrayType arr => WithoutNil(arr.Element), TableType dict => WithoutNil(dict.Value), _ => null } is { } inner && inner is ArrayType or TableType ? inner : null;
					if (target is not null) break;
				}
				table.TargetType = target ?? table.DeclaredType;
			}
		foreach (var table in tables) {
			if (table.TargetType is not { } target) continue;
			var element = target switch { ArrayType arr => arr.Element, TableType dict => dict.Value, _ => null };
			var key = (target as TableType)?.Key;
			foreach (var field in table.Fields) {
				if (element is not null && !IsAny(element)) {
					field.Value.Expected = element;
					var actual = Value(field.Value);
					if (!IsAny(actual) && field.Value.IsEmpty is false && m_checker.Check(actual, element).Kind == Compat.Blocked)
						Report(DiagnosticSeverity.Error, $"this {(target is ArrayType ? "array" : "dictionary")} holds {TypeFormatter.Display(element)}, not {TypeFormatter.Display(actual)}", table, field.Value);
				}
				if (key is not null && field.Key is { } keyPin) {
					keyPin.Expected = key;
					var actualKey = Value(keyPin);
					if (!IsAny(actualKey) && m_checker.Check(actualKey, key).Kind == Compat.Blocked)
						Report(DiagnosticSeverity.Error, $"the keys of this dictionary are {TypeFormatter.Display(key)}, not {TypeFormatter.Display(actualKey)}", table, keyPin);
				}
			}
			if (target is ArrayType or TableType) table.Result.Type = target;
		}
	}

	private static LuaType Substitute(LuaType type, IReadOnlyDictionary<string, LuaType> map) {
		switch (type) {
			case NamedType { Args.Count: 0 } n when map.TryGetValue(n.Name, out var replacement):
				return replacement;
			case NamedType n when n.Args.Count > 0:
				return n with { Args = n.Args.Select(a => Substitute(a, map)).ToList() };
			case UnionType u:
				return LuaType.Union(u.Members.Select(m => Substitute(m, map)).ToArray());
			case ArrayType a:
				return new ArrayType(Substitute(a.Element, map));
			case TableType t:
				return new TableType(Substitute(t.Key, map), Substitute(t.Value, map));
			case FunctionType f:
				return new FunctionType(f.Params.Select(p => p with { Type = Substitute(p.Type, map) }).ToList(), f.Returns.Select(r => Substitute(r, map)).ToList(),
					f.VarargParam is null ? null : Substitute(f.VarargParam, map), f.VarargReturn is null ? null : Substitute(f.VarargReturn, map));
			default:
				return type;
		}
	}

	private MemberDecl? LookupMember(LuaType receiver, string name, InputPin receiverPin, GraphNode node) {
		WarnIfNullable(receiverPin, receiver, node);
		var type = NonNil(receiver);
		string? className = null;
		var map = new Dictionary<string, LuaType>();
		switch (type) {
			case NamedType named when m_universe.FindClass(named.Name) is { } info:
				className = named.Name;
				for (var i = 0; i < info.Generics.Count && i < named.Args.Count; i++) map[info.Generics[i].Name] = named.Args[i];
				break;
			case NamedType named when m_universe.FindEnum(named.Name) is not null:
				return new MemberDecl(name, named, "", null, false, [], [], new SourceLocation("", 0));
			case PrimitiveType { Kind: PrimitiveKind.String }:
				className = "stringlib";
				break;
		}
		if (className is null) return null;
		var found = m_universe.MembersOf(className).FirstOrDefault(m => m.Decl.Name == name)?.Decl;
		if (found is null || map.Count == 0) return found;
		return found with { Type = Substitute(found.Type, map), Overloads = found.Overloads.Select(o => (FunctionType)Substitute(o, map)).ToList() };
	}

	private static MemberDecl WithoutFirstParam(MemberDecl member) {
		if (member.Type is not FunctionType { Params.Count: > 0 } fn) return member;
		return member with {
			Type = fn with { Params = fn.Params.Skip(1).ToList() },
			Overloads = member.Overloads.Select(o => o.Params.Count > 0 ? o with { Params = o.Params.Skip(1).ToList() } : o).ToList(),
		};
	}

	private static FunctionType WithReceiver(FunctionType signature, LuaType receiver)
		=> signature with { Params = signature.Params.Prepend(new Param("self", receiver, false)).ToList() };

	private MemberDecl? CalleeOf(CallNode call) {
		var callee = Value(call.Target);
		var source = call.Target.Source;
		if (source is { Binding: null, Owner: GlobalGet global } && m_universe.GlobalDeclOf(global.Name) is { Type: FunctionType type } declared)
			return new MemberDecl(global.Name, type, declared.Doc, null, false, declared.Generics, declared.Overloads, declared.Location);
		if (source is { Binding: null, Owner: MemberGet member }) {
			var found = LookupMember(member.Object.ActualType, member.Name, member.Object, call);
			if (found is null || found.Type is not FunctionType) return null;
			if (!found.IsMethod) return found;
			var receiver = NonNil(member.Object.ActualType);
			return found with {
				Type = WithReceiver((FunctionType)found.Type, receiver),
				Overloads = found.Overloads.Select(o => WithReceiver(o, receiver)).ToList()
			};
		}
		if (source?.Binding is { } binding && m_functions.TryGetValue(binding, out var local)) return local;
		return callee is FunctionType function ? new MemberDecl("function", function, "", null, false, [], [], new SourceLocation("", 0)) : null;
	}

	private void TypeCall(CallNode call) {
		var argumentTypes = call.Args.Select(Value).ToList();
		MemberDecl? member;
		if (call.Style == CallStyle.Method) {
			var receiver = Value(call.Target);
			member = LookupMember(receiver, call.Method!, call.Target, call);
			if (member is not null && !member.IsMethod) member = WithoutFirstParam(member);
		} else {
			member = CalleeOf(call);
		}

		call.ReturnCount = 0;
		if (member is null || member.Type is not FunctionType) {
			call.Result.Type = LuaType.Any;
			foreach (var output in call.Outputs) output.Type = LuaType.Any;
			return;
		}

		var resolved = m_resolver.Resolve(member, argumentTypes.Cast<LuaType?>().ToList());
		var signature = resolved.Signature;
		for (var i = 0; i < call.Args.Count; i++) {
			var index = i;
			var parameter = index < signature.Params.Count ? signature.Params[index] : null;
			var expected = parameter is null ? signature.VarargParam : (parameter.Optional ? LuaType.Optional(parameter.Type) : parameter.Type);
			call.Args[i].Expected = expected;
			var result = index < resolved.ArgResults.Count ? resolved.ArgResults[index] : CompatResult.Ok;
			if (result.Kind != Compat.Allowed)
				Report(result.Kind == Compat.Warning ? DiagnosticSeverity.Warning : DiagnosticSeverity.Error, result.Reason ?? "argument does not fit", call, call.Args[i]);
		}

		var returns = signature.Returns;
		var targetBinding = call.Target.Source?.Binding ?? (call.Target.Source?.Owner as VariableGet)?.Binding;
		call.ReturnCount = Math.Max(returns.Count, targetBinding is not null ? m_returnCounts.GetValueOrDefault(targetBinding) : 0);
		call.EnsureOutputs(Math.Max(1, call.ReturnCount));
		for (var i = 0; i < call.Outputs.Count; i++)
			call.Outputs[i].Type = i < returns.Count ? returns[i] : (signature.VarargReturn ?? (i == 0 ? LuaType.Nil : LuaType.Any));
	}

	private static string? OperatorName(BinaryKind op) => op switch {
		BinaryKind.Add => "add", BinaryKind.Sub => "sub", BinaryKind.Mul => "mul", BinaryKind.Div => "div", BinaryKind.Mod => "mod",
		BinaryKind.Pow => "pow", BinaryKind.FloorDiv => "idiv", BinaryKind.Concat => "concat", _ => null,
	};

	private LuaType? OverloadedResult(string op, LuaType owner, LuaType? operand) {
		if (NonNil(owner) is not NamedType named || m_universe.FindClass(named.Name) is null) return null;
		foreach (var candidate in m_universe.OperatorsOf(named.Name).Where(o => o.Op == op)) {
			if (operand is null) return candidate.Result;
			if (candidate.Operand is null) continue;
			if (m_checker.Check(operand, candidate.Operand).Kind != Compat.Blocked) return candidate.Result;
		}
		return null;
	}

	private static bool IsNumeric(LuaType type) => type == LuaType.Number || type == LuaType.Integer;

	private void TypeBinary(BinaryOp binary) {
		var left = Value(binary.Left);
		LuaType right;
		if (binary.Op is BinaryKind.And or BinaryKind.Or) {
			// short circuit evaluation narrows the right operand
			var inner = new Env(m_env);
			Narrow(binary.Left, binary.Op == BinaryKind.And, inner);
			var saved = m_env;
			m_env = inner;
			right = Value(binary.Right);
			m_env = saved;
		} else {
			right = Value(binary.Right);
		}

		binary.Result.Type = binary.Op switch {
			BinaryKind.And => HasNil(left) ? LuaType.Union(LuaType.Nil, right) : (IsAny(left) ? LuaType.Any : right),
			BinaryKind.Or => HasNil(left) ? LuaType.Union(NonNil(left), right) : left,
			BinaryKind.Eq or BinaryKind.Ne or BinaryKind.Lt or BinaryKind.Le or BinaryKind.Gt or BinaryKind.Ge => LuaType.Boolean,
			BinaryKind.Concat => OverloadedResult("concat", left, right) ?? LuaType.String,
			BinaryKind.BitAnd or BinaryKind.BitOr or BinaryKind.BitXor or BinaryKind.Shl or BinaryKind.Shr => LuaType.Integer,
			_ => Arithmetic(binary.Op, left, right),
		};
	}

	private LuaType Arithmetic(BinaryKind op, LuaType left, LuaType right) {
		var l = NonNil(left);
		var r = NonNil(right);
		if (IsNumeric(l) && IsNumeric(r))
			return op is BinaryKind.Div or BinaryKind.Pow ? LuaType.Number : (l == LuaType.Integer && r == LuaType.Integer ? LuaType.Integer : LuaType.Number);
		var name = OperatorName(op)!;
		return OverloadedResult(name, l, r) ?? OverloadedResult(name, r, l) ?? (l == LuaType.String || r == LuaType.String ? LuaType.Number : LuaType.Any);
	}

	private void TypeUnary(UnaryOp unary) {
		var operand = NonNil(Value(unary.Operand));
		unary.Result.Type = unary.Op switch {
			UnaryKind.Not => LuaType.Boolean,
			UnaryKind.Len => OverloadedResult("len", operand, null) ?? LuaType.Integer,
			UnaryKind.BitNot => LuaType.Integer,
			_ => IsNumeric(operand) ? operand : OverloadedResult("unm", operand, null) ?? LuaType.Any,
		};
	}
}
