using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Layout;
using editor.LuaEditor.Types;

namespace editor.LuaEditor.Editing;

public sealed partial class GraphSession {
	public static IReadOnlyList<string> TickFunctions { get; } = Array.AsReadOnly(new[] {
		"load", "init", "begin", "earlyTick", "tick", "physicsTick", "lateTick", "_end", "destroy",
		"save", "editorTick", "onEnable", "onDisable", "postPhysics"
	});

	public IEnumerable<ClassInfo> SourceClasses() {
		var pending = new Queue<string>();
		if (ScriptInfo.Find(Graph) is { } script)
			foreach (var parent in (script.ClassTag?.Bases ?? []).OfType<NamedType>()) pending.Enqueue(parent.Name);
		var seen = new HashSet<string>(StringComparer.Ordinal);
		while (pending.TryDequeue(out var name)) {
			if (!seen.Add(name) || Universe.FindClass(name) is not { } info) continue;
			yield return info;
			foreach (var parent in info.Bases.OfType<NamedType>()) pending.Enqueue(parent.Name);
		}
	}

	public NodeEntry HierarchyEntry(GraphVariable variable, Binding? binding, MemberInfo? native, bool set, bool dot) {
		var name = variable.Name;
		if (variable.Kind == VariableKind.Function || binding is not null && variable.Type is FunctionType) {
			var colon = native?.Decl.IsMethod ?? (variable.Declaration as FunctionDecl)?.IsMethod ?? false;
			var dotCall = binding is null && (dot || native is { Decl.IsMethod: false });
			var signature = variable.Type as FunctionType;
			if (dotCall && colon && signature is not null) {
				var owner = native?.DeclaringClass ?? ScriptInfo.Find(Graph)?.ClassName ?? ScriptInfo.UnnamedClass;
				signature = signature with { Params = signature.Params.Prepend(new Param("self", new NamedType(owner, []), false)).ToList() };
			}
			if (binding is null && !dotCall && !colon && signature is not null)
				signature = signature with { Params = signature.Params.Skip(1).ToList() };
			return new NodeEntry(dotCall || binding is not null ? NodeKind.CallGlobal : NodeKind.CallMethod, name + "()",
				native is null ? "Script functions" : "C++ " + native.DeclaringClass, name,
				Name: name, Owner: native?.DeclaringClass ?? ScriptOwner, Binding: binding, Signature: signature, DotCall: dotCall);
		}
		return new NodeEntry(set ? NodeKind.SetVariable : NodeKind.GetVariable, (set ? "Set " : "Get ") + name,
			"Variables", name, Name: name, Owner: native is not null ? BaseOwner : variable.Kind.ToString(),
			Result: set ? null : variable.Type, FirstInput: set ? variable.Type : null, Binding: binding);
	}

	private void BindHierarchyCall(NodeEntry entry, CallNode call, Block? block) {
		if (entry.Binding is not null || !IsOwnCall(entry) || ObjectSource(block) is not { } owner) return;
		if (entry.DotCall) {
			var member = new MemberGet(Graph, entry.Name!);
			member.Object.Connect(owner);
			call.Target.Connect(member.Result);
			if (entry.Signature?.Params.FirstOrDefault()?.Name == "self" && call.Args.Count > 0) call.Args[0].Connect(owner);
		} else {
			ConnectCore(owner, call.Target);
		}
	}

	public bool DuplicateField(AssignStatement field, string name) => Edit(() => {
		if (ScriptInfo.Find(Graph)?.Fields.Any(f => f.Statement == field) != true)
			throw new InvalidOperationException("the field no longer exists");
		var block = field.Parent!;
		var index = block.IndexOf(field) + 1;
		var result = Fragments.Paste(Graph, Fragments.Copy([field]), block, index);
		if (result.Errors.Count > 0) throw new InvalidOperationException(result.Errors[0].Message);
		if (block.Statements[index] is not AssignStatement copy)
			throw new InvalidOperationException("could not duplicate the field");
		copy.Targets[0].Name = name;
	});

	public static InputPin? VariableDefault(GraphVariable variable, Binding? binding) =>
		variable.Declaration switch {
			AssignStatement assignment when variable.Kind == VariableKind.Field => assignment.Values.FirstOrDefault(),
			LocalDecl local when binding is not null && local.Names.IndexOf(binding) is var index && index >= 0 =>
				local.Values.ElementAtOrDefault(index),
			_ => null
		};

	public static LuaConstant? LiteralDefault(InputPin? pin) => pin?.Constant ??
		(pin?.Source?.Owner is UnaryOp { Op: UnaryKind.Neg, Operand.Constant: { Kind: ConstantKind.Number } number }
			? LuaConstant.Number("-" + number.Raw) : null);

	public bool SetVariableDefault(GraphVariable variable, Binding? binding, LuaConstant value) => Edit(() => {
		var pin = EnsureVariableDefault(variable, binding);
		if (pin.Source is not null && LiteralDefault(pin) is null)
			throw new InvalidOperationException("this variable has an expression initializer; use init() for graph initialization");
		Graph.SetConstant(pin, value);
	});

	public bool SetVariableType(GraphVariable variable, Binding? binding, LuaType? type) => Edit(() => {
		if (variable.Declaration is LocalDecl local && binding != local.Names[0])
			throw new InvalidOperationException("declare this local on its own line to set its type");
		Graph.SetDeclaredType(variable.Declaration, type);
		if (type is null) return;

		var resolved = Universe.ResolveAliases(type);
		if (resolved is UnionType union && union.Members.Count(member => member != LuaType.Nil) == 1)
			resolved = union.Members.First(member => member != LuaType.Nil);
		var initial = VariableDefault(variable, binding);
		var isMarker = initial?.Source?.Owner is GlobalGet old && Universe.GlobalType(old.Name) is NamedType { Name: "TypeMarker" };
		var literal = LiteralDefault(initial);
		if (initial is { Source: not null } && literal is null && !isMarker) return;

		var value = resolved switch {
			PrimitiveType { Kind: PrimitiveKind.Integer } => LuaConstant.Integer(0),
			PrimitiveType { Kind: PrimitiveKind.Number } => LuaConstant.Float(0),
			PrimitiveType { Kind: PrimitiveKind.Boolean } => LuaConstant.False,
			PrimitiveType { Kind: PrimitiveKind.String } => LuaConstant.String(""),
			_ => null
		};
		if (value is not null) {
			if (literal is { Kind: not ConstantKind.Nil } && m_checker.Check(literal.ValueType, resolved).Kind == Compat.Allowed) return;
			Graph.SetConstant(EnsureVariableDefault(variable, binding), value);
		} else if (variable is { Kind: VariableKind.Field, Exported: true } && resolved is NamedType named &&
			Universe.GlobalType(named.Name) is NamedType { Name: "TypeMarker", Args: [var target] } && target == resolved &&
			variable.Declaration.Parent is { } block &&
			!LuaGraph.VisibleFromBlock(block, block.IndexOf(variable.Declaration)).Any(visible => visible.Name == named.Name)) {
			Graph.Connect(new GlobalGet(Graph, named.Name).Result, EnsureVariableDefault(variable, binding));
		} else if (initial is not null && (isMarker || literal is { Kind: not ConstantKind.Nil } &&
			(resolved is ArrayType or TableType or PrimitiveType { Kind: PrimitiveKind.Table } ||
				resolved is NamedType reference && Universe.GlobalType(reference.Name) is NamedType { Name: "TypeMarker" }))) {
			Graph.SetConstant(initial, LuaConstant.Nil);
		}
	});

	private InputPin EnsureVariableDefault(GraphVariable variable, Binding? binding) {
		if (VariableDefault(variable, binding) is { } existing) return existing;
		if (variable.Declaration is not LocalDecl local || binding is null || local.Names.IndexOf(binding) is not (>= 0 and var index))
			throw new InvalidOperationException("this declaration has no editable default");
		if (local.Values.LastOrDefault()?.Source is { Binding: null, Index: 0, Owner: CallNode or VarargNode })
			throw new InvalidOperationException("this local gets its value from a multi-value initializer");
		while (local.Values.Count <= index) local.AddValue().SetConstant(LuaConstant.Nil);
		return local.Values[index];
	}

	public bool MakeLocalFunction(Binding binding) => Edit(() => {
		if (binding.Declarer is not LocalDecl { Names.Count: 1 } local || local.Parent != Graph.Module ||
			binding.Attribute is not null || binding.IsMutable ||
			local.Values.Any(pin => pin.Source is not null || pin.Constant is { Kind: not ConstantKind.Nil }))
			throw new InvalidOperationException("choose an unassigned local with no initial value to create a function");
		var index = Graph.Module.IndexOf(local);
		var function = new FunctionDecl(Graph, true, binding.Name);
		function.LeadingLines.AddRange(local.LeadingLines);
		Graph.SetDeclaredType(function, null);
		var oldOutput = binding.Output;
		function.AdoptLocalBinding(binding);
		foreach (var consumer in oldOutput.Consumers.ToList()) consumer.Connect(binding.Output);
		local.Names.Clear();
		Graph.Remove(local);
		Graph.Module.Insert(index, function);
	});
}
