using System.Runtime.CompilerServices;
using Proto.Events;

namespace editor.Workspace;

// previewer
public partial class SignalConnectionDialogViewModel {
	public SignalConnectionDialogViewModel() {
		SignalText = "hit(float, std::string, glm::vec3)";
		SignalArgs = [new SignalArgChip(0, "float"), new SignalArgChip(1, "std::string"), new SignalArgChip(2, "glm::vec3")];

		Nodes = [DesignNodes()];
		m_selectedNode = Nodes[0].Children[0].Children[1];

		Replace([
			Callable("Lua", "applyHit",
				("damage", "number"), ("label", "string"), ("knockback", "glm::vec3"), ("enabled", "boolean"),
				("count", "integer"), ("tint", "any")),
			Callable("Lua", "flash", ("duration", "number")),
			Callable("Enemy", "heal", ("amount", "int")),
			Callable("Enemy", "setTarget", ("position", "glm::vec3")),
			Callable("Enemy", "getHealth", disabled: "The function returns a value"),
			Callable("Node3D", "setPosition", ("position", "glm::vec3")),
			Callable("Node3D", "setScale", ("scale", "glm::vec3")),
			Callable("Node3D", "setPath", ("points", "std::vector<glm::vec3>"),
				disabled: "Parameter 'points' is a container, which cannot be bound"),
			Callable("Node", "setEnabled", ("enabled", "bool")),
			Callable("Node", "setName", ("name", "std::string")),
			Callable("Node", "destroy", connected: true)
		]);
		EmptyMessage = "";
		SelectedFunction = Functions[0];

		Parameters[0].BoundArg = 0;
		Parameters[1].StringValue = "Critical hit";
		Parameters[2].X = 0;
		Parameters[2].Y = 4.5f;
		Parameters[2].Z = 0;
	}

	private static SignalFunctionViewModel Callable(
		string declaringClass, string name, params (string Name, string Type)[] parameters) =>
		Callable(declaringClass, name, parameters, "", false);

	private static SignalFunctionViewModel Callable(
		string declaringClass, string name, (string Name, string Type) parameter, string disabled) =>
		Callable(declaringClass, name, [parameter], disabled, false);

	private static SignalFunctionViewModel Callable(string declaringClass, string name, string disabled) =>
		Callable(declaringClass, name, [], disabled, false);

	private static SignalFunctionViewModel Callable(string declaringClass, string name, bool connected) =>
		Callable(declaringClass, name, [], "", connected);

	private static SignalFunctionViewModel Callable(
		string declaringClass, string name, (string Name, string Type)[] parameters, string disabled, bool connected) {
		var callable = new SignalCallable {
			Name = name,
			DeclaringClass = declaringClass,
			HasCpp = declaringClass != "Lua",
			HasLua = declaringClass == "Lua",
			Compatible = disabled.Length == 0,
			AlreadyConnected = connected,
			ForwardsArgs = parameters.Length > 0,
			DisabledReason = connected ? "Already connected" : disabled
		};
		foreach (var (parameterName, type) in parameters) callable.Parameters.Add(new SignalParameter { Name = parameterName, Type = type });
		return new SignalFunctionViewModel(callable);
	}

	private static HierarchyElement DesignNodes() {
		var owner = (HierarchyViewModel)RuntimeHelpers.GetUninitializedObject(typeof(HierarchyViewModel));
		var root = new Proto.Events.HierarchyElement { Uid = "root", Name = "Level 1", Type = "Node" };
		var world = new Proto.Events.HierarchyElement { Uid = "world", Name = "World", Type = "Node" };
		world.Children.Add(new Proto.Events.HierarchyElement { Uid = "camera", Name = "Camera", Type = "Camera" });
		world.Children.Add(new Proto.Events.HierarchyElement { Uid = "enemy", Name = "Enemy", Type = "Node3D" });
		world.Children.Add(new Proto.Events.HierarchyElement { Uid = "sun", Name = "Sun", Type = "Light" });
		root.Children.Add(world);
		root.Children.Add(new Proto.Events.HierarchyElement { Uid = "ui", Name = "HUD", Type = "Node" });
		return new HierarchyElement(root, owner, null, true);
	}
}
