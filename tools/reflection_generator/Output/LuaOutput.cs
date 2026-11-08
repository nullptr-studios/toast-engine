using ReflectionGenerator.Analysis;
using ReflectionGenerator.Model;
using ReflectionGenerator.Rules;

namespace ReflectionGenerator.Output;

internal sealed record LuaMember(string Name, string LuaType);

internal sealed record LuaNodeView(string Name, string Inheritance, IReadOnlyList<LuaMember> Fields, IReadOnlyList<LuaMember> Methods, IReadOnlyList<LuaMember> Signals);

internal sealed record LuaEventView(string Name, bool Sendable, IReadOnlyList<LuaMember> Fields);

internal static class LuaOutput
{
	public static string NodeStubs(Templates templates, IEnumerable<NodeType> nodes) =>
		templates.Render("types.d.lua.scriban", "nodes", nodes.Select(NodeStub).ToList());

	public static string EventStubs(Templates templates, IEnumerable<EventType> events) =>
		templates.Render("events.d.lua.scriban", "events", events.Where(e => e.Supported).Select(EventStub).ToList());

	private static LuaNodeView NodeStub(NodeType node) => new(
		node.Name,
		node.Parent is { } parent ? $" : {parent.Name.Bare()}" : "",
		// hidden fields are not in the inspector and would shadow builtins like uid() and name()
		FieldLayout.From(node.Fields).InOrder
			.Where(f => !f.Attributes.Has("Hidden"))
			.Select(f => new LuaMember(f.Name.WithoutPrefix("m_"), LuaTypes.ForField(f)))
			.ToList(),
		node.Methods.Select(m => new LuaMember(m.Name, LuaTypes.ForMethod(node.Name, m))).ToList(),
		node.Signals.Select(s => new LuaMember(s.Name, LuaTypes.ForSignal(s))).ToList());

	private static LuaEventView EventStub(EventType e) => new(
		e.Name,
		e.Sendable,
		e.Fields.Select(f => new LuaMember(f.Name, f.LuaType ?? "any")).ToList());
}
