using ReflectionGenerator.Analysis;
using ReflectionGenerator.Model;
using ReflectionGenerator.Rules;

namespace ReflectionGenerator.Output;

// templates only lay text out so every decision is made here

internal sealed record FieldView(int Index, string Name, string TypeName, string Kind, bool IsArray, string Accessor, string AttributesInit)
{
	public static FieldView From(Field field, int index)
	{
		var isAsset = field.TypeName.Contains("Handle<");
		var isEnum = field.Attributes.Has("Enum") && !field.IsArray;
		var accessor = (isAsset, isEnum, field.IsArray) switch {
			(true, _, true) => "AssetArrayFieldAccess",
			(true, _, false) => "AssetFieldAccess",
			(false, true, _) => "EnumFieldAccess",
			_ => "FieldAccess",
		};
		return new FieldView(index, field.Name, field.TypeName, field.Kind.Name(), field.IsArray, accessor, CppText.Attributes(field.Attributes));
	}
}

internal sealed record SubgroupView(string Name, string Slug, IReadOnlyList<FieldView> Fields);

internal sealed record GroupView(string Name, string Slug, IReadOnlyList<FieldView> Fields, IReadOnlyList<SubgroupView> Subgroups);

internal sealed record TickFunctionView(string Flag, string Method);

internal sealed record SignalView(string Name, string TypeName, string TypeIdsInit, string AttributesInit);

internal sealed record ParameterView(int Index, string Name, string Type, string ArgName, string DefaultInit, string DynamicValue)
{
	public static ParameterView From(Parameter parameter, int index)
	{
		var cast = DynamicCast(parameter.Type, index);
		var value = parameter.Default is null
			? cast
			: $"((args.size() > {index} && args[{index}].has_value()) ? static_cast<std::decay_t<{parameter.Type}>>({cast}) : static_cast<std::decay_t<{parameter.Type}>>({parameter.Default}))";
		return new ParameterView(index, parameter.Name, parameter.Type, $"a{index}", CppText.DefaultValue(parameter.Default), value);
	}

	// arguments arrive from lua as std::any so every parameter kind needs its own way back to a c++ value
	private static string DynamicCast(string type, int index)
	{
		if (type.Contains("Box<")) {
			var node = type.Replace("toast::Box<", "").Replace("Box<", "").Replace("const ", "").Replace("&", "").Replace(">", "").Trim();
			return $"std::any_cast<toast::Box<toast::Node>>(args[{index}]).as<{node}>()";
		}
		if (type.Contains("Handle<")) {
			return $"assets::load<typename std::decay_t<{type}>::asset_type>(std::any_cast<toast::UID>(args[{index}]))";
		}
		return $"std::any_cast<std::decay_t<{type}>>(args[{index}])";
	}
}

internal sealed record MethodView(
	string Name,
	string ReturnType,
	bool ReturnsVoid,
	string PointerType,
	string InvokerParameters,
	string ArgumentNames,
	string DynamicArguments,
	string AttributesInit,
	IReadOnlyList<ParameterView> Parameters)
{
	public static MethodView From(Method method, string qualifiedClassName)
	{
		var parameters = method.Parameters.Select(ParameterView.From).ToList();
		var types = string.Join(", ", parameters.Select(p => p.Type));
		var constSuffix = method.IsConst ? " const" : "";

		return new MethodView(
			method.Name,
			method.ReturnType,
			method.ReturnType == "void",
			$"{method.ReturnType} ({qualifiedClassName}::*)({types}){constSuffix}",
			string.Concat(parameters.Select(p => $", std::decay_t<{p.Type}> {p.ArgName}")),
			string.Join(", ", parameters.Select(p => p.ArgName)),
			string.Join(", ", parameters.Select(p => p.DynamicValue)),
			CppText.Attributes(method.Attributes),
			parameters);
	}
}

internal sealed record NodeView(
	string Name,
	string Slug,
	string QualifiedName,
	string? ParentQualifiedName,
	string? ParentSlug,
	string SourceFile,
	bool IsInterface,
	bool HasAssetHandle,
	bool SplitTypeInfo,
	IReadOnlyList<FieldView> AllFields,
	IReadOnlyList<FieldView> GlobalFields,
	IReadOnlyList<GroupView> Groups,
	IReadOnlyList<TickFunctionView> TickFunctions,
	string TickList,
	IReadOnlyList<MethodView> Methods,
	IReadOnlyList<SignalView> Signals)
{
	public static NodeView From(NodeType node, bool splitTypeInfo)
	{
		var qualified = node.QualifiedName;
		var allFields = new List<FieldView>();

		FieldView Add(Field field)
		{
			var view = FieldView.From(field, allFields.Count);
			allFields.Add(view);
			return view;
		}

		// the order fields are added in is the index they get in the generated array
		var layout = FieldLayout.From(node.Fields);
		var global = layout.GlobalFields.Select(Add).ToList();
		var groups = layout.Groups.Select(group => new GroupView(
			group.Name,
			GroupSlug(group.Name),
			group.Fields.Select(Add).ToList(),
			group.Subgroups.Select(sub => new SubgroupView(sub.Name, GroupSlug(sub.Name), sub.Fields.Select(Add).ToList())).ToList()
		)).ToList();

		var tickFunctions = Rules.TickFunctions.All
			.Where(t => node.TickFunctions.Contains(t.Method))
			.Select(t => new TickFunctionView(t.Flag, t.Method))
			.ToList();

		var hasAssetHandle = node.Fields.Any(f => f.TypeName.Contains("Handle<"))
			|| node.Methods.Any(m => m.Parameters.Any(p => p.Type.Contains("Handle<")));

		return new NodeView(
			node.Name,
			node.Name.ToLowerInvariant(),
			qualified,
			node.Parent is { } parent ? Strings.Qualify(parent.Namespace, parent.Name) : null,
			node.Parent?.Name.ToLowerInvariant(),
			node.SourceFile,
			node.IsInterface,
			hasAssetHandle,
			splitTypeInfo,
			allFields,
			global,
			groups,
			tickFunctions,
			tickFunctions.Count == 0 ? "TickFunctionList::none" : string.Join(" | ", tickFunctions.Select(t => $"TickFunctionList::{t.Flag}")),
			node.Methods.Select(m => MethodView.From(m, qualified)).ToList(),
			node.Signals.Select(s => new SignalView(
				s.Name,
				s.TypeName,
				CppText.TypeIds(s.Arguments),
				CppText.Attributes(s.Attributes))).ToList());
	}

	// group names become part of c++ identifiers
	private static string GroupSlug(string name) => name.ToLowerInvariant().Replace(' ', '_');
}

internal sealed record EventFieldView(string Name, string TypeName);

internal sealed record EventView(string Name, string QualifiedName, string SourceFile, bool Sendable, IReadOnlyList<EventFieldView> Fields)
{
	public static EventView From(EventType e) => new(
		e.Name,
		e.QualifiedName,
		e.SourceFile,
		e.Sendable,
		e.Fields.Select(f => new EventFieldView(f.Name, f.TypeName)).ToList());
}

internal sealed record RegistryView(IReadOnlyList<NodeView> Nodes, IReadOnlyList<EventView> Events, string RegisterFn, bool SplitTypeInfo);
