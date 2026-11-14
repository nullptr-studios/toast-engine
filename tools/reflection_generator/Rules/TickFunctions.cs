namespace ReflectionGenerator.Rules;

internal sealed record TickFunction(string Method, string Flag);

internal static class TickFunctions
{
	// keep in step with TickFunctionList in reflect.hpp and this order is the generated order
	public static readonly IReadOnlyList<TickFunction> All = [
		new("load", "load"),
		new("save", "save"),
		new("editorTick", "editor_tick"),
		new("init", "init"),
		new("destroy", "destroy"),
		new("begin", "begin"),
		new("end", "end"),
		new("onEnable", "on_enable"),
		new("onDisable", "on_disable"),
		new("earlyTick", "early_tick"),
		new("tick", "tick"),
		new("physicsTick", "physics_tick"),
		new("postPhysics", "post_physics"),
		new("lateTick", "late_tick"),
	];

	public static bool IsTickFunction(string method) => All.Any(t => t.Method == method);
}
