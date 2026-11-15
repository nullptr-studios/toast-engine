using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Graph;

namespace editor.LuaEditor.Layout;

public static class InlineRules {
	public static GraphNode? HostOf(GraphNode node) {
		if (node is FunctionEntry { Owner: FunctionDecl declaration }) return declaration;
		if (node is MemberGet enumMember && Sole(enumMember.Result) is { } enumPin && EnumValueOf(enumPin) is not null) return enumPin.Owner;
		if (node is CallNode call && Sole(call.Result) is { } used) {
			if (used.Owner is CallStatement statement && statement.Call == used) return statement;
			if (used.Owner is LocalDecl { Values: [var initializer] } local && initializer == used &&
				call.Style == CallStyle.Function && (NodeShape.BindingOf(call.Target.Source)?.Declarer is FunctionDecl { IsLocal: true } || call.Target.Source?.Owner is MemberGet)) return local;
		}
		if (IsStaticName(node) && Sole(((ExprNode)node).Result) is { } pin) {
			if (pin.Owner is MemberGet member && member.Object == pin) return member;
			if (pin.Owner is CallNode caller && caller.Target == pin && caller.Style == CallStyle.Function) return caller;
		}
		return null;
	}

	public static string? EnumValueOf(InputPin pin) {
		if (pin.Source?.Owner is MemberGet member && member.Object.Source?.Owner is GlobalGet table && Sole(member.Result) == pin
		    && pin.Owner.Graph.Universe.Enums.Any(e => e.TableName == table.Name)) return member.Name;
		return null;
	}

	public static string? InlineValueOf(InputPin pin) => EnumValueOf(pin);

	private static InputPin? Sole(OutputPin output)
		=> output.Consumers.Count == 1 ? output.Consumers[0] : null;

	private static bool IsStaticName(GraphNode node) =>
		node is GlobalGet || node is MemberGet { Object.Source.Owner: { } inner } member && IsStaticName(inner) && Sole(((ExprNode)inner).Result) == member.Object;

	public static bool IsMerged(GraphNode node) => HostOf(node) is not null;

	public static bool IsMergedName(GraphNode node) => node is not FunctionEntry && IsMerged(node);

	public static bool IsGetterNode(GraphNode node) => !IsMerged(node) && (node is VariableGet || node is MemberGet);

	public static bool IsImplicitObject(InputPin pin) {
		if (pin.Source?.Binding is not { } binding) return false;
		var inObjectPosition = pin.Owner switch {
			CallNode c => c.Style == CallStyle.Method && c.Target == pin,
			AssignStatement a => a.Targets.Any(t => t.Object == pin),
			_ => false
		};
		return inObjectPosition && (binding.Kind == BindingKind.Self || IsScriptTable(binding));
	}

	private static bool IsScriptTable(Binding binding) {
		if (binding.Declarer is not LocalDecl declaration || declaration.Parent != declaration.Graph.Module) return false;
		var last = declaration.Graph.Module.Statements.LastOrDefault(s => s is not (CommentStatement or CastStatement));
		return last is ReturnStatement { Values.Count: 1 } ret && ret.Values[0].Source?.Binding == binding;
	}

	public static string? ImplicitObjectName(InputPin pin) => IsImplicitObject(pin) ? pin.Source!.Binding!.Name : null;

	public static GraphNode Drawn(GraphNode node) {
		var current = node;
		for (var guard = 0; guard < 64 && HostOf(current) is { } host; guard++) current = host;
		return current;
	}

	public static IReadOnlyList<InputPin> VisibleInputs(GraphNode node) {
		switch (node) {
			case Statement when NodeShape.CallOf(node) is { } call && IsMerged(call):
				return VisibleInputs(call);
			case CallNode call:
				return call.Inputs.Where(p => !(p == call.Target && (IsImplicitObject(p) || p.Source is { Binding.Kind: BindingKind.LocalFunction } || p.Source?.Owner is { } target && IsMergedName(target)))).ToList();
			case MemberGet member when IsImplicitObject(member.Object) || member.Object.Source?.Owner is { } inner && IsMergedName(inner):
				return [];
			case TableNode table:
				return table.Inputs.Where(p => !table.Fields.Any(f => f.Key == p && p.Source is null)).ToList();
			case AssignStatement assign when assign.Targets.Any(t => t.Object is { } o && IsImplicitObject(o)):
				return assign.Inputs.Where(p => !IsImplicitObject(p)).ToList();
			default:
				return node.Inputs;
		}
	}

	public static int RowOf(InputPin pin) {
		var host = Drawn(pin.Owner);
		var visible = VisibleInputs(host);
		for (var i = 0; i < visible.Count; i++)
			if (visible[i] == pin) return i;
		return -1;
	}

	public static string? Title(GraphNode node) {
		switch (node) {
			case Statement when NodeShape.CallOf(node) is { } call && IsMerged(call):
				return Title(call);
			case CallNode call:
				if (call.Style == CallStyle.Method) return (ImplicitObjectName(call.Target) ?? "") + ":" + call.Method;
				if (NodeShape.BindingOf(call.Target.Source) is { Kind: BindingKind.LocalFunction } local) return local.Name;
				if (call.Target.Source?.Owner is MemberGet memberTarget) return Title(memberTarget);
				return call.Target.Source?.Owner is { } target && IsMergedName(target) ? Title(target) : call.Method ?? "call";
			case GlobalGet global:
				return global.Name;
			case MemberGet member:
				if (ImplicitObjectName(member.Object) is { } owner) return owner + "." + member.Name;
				return member.Object.Source?.Owner is { } inner && IsMergedName(inner) ? Title(inner) + "." + member.Name : member.Name;
		}
		return null;
	}
}
