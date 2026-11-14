using ReflectionGenerator.Model;

namespace ReflectionGenerator.Rules;

internal static class Validation
{
	private static readonly string[] ReservedFieldNames = [
		"m_uid",
		"m_name",
		"m_local_enabled",
		"m_parent",
		"m_source_prefab",
	];

	public static void Check(NodeType node)
	{
		var name = node.QualifiedName;

		// the base node is the one class allowed to own the reserved members
		if (name == "toast::Node") return;

		foreach (var field in node.Fields) {
			if (ReservedFieldNames.Contains(field.Name)) {
				throw new GeneratorException(
					$"{name}: reflected field '{field.Name}' uses a reserved engine member name (only toast::Node may)");
			}

			var group = field.Attributes.FirstArg("Group");
			if (group is not null && group.StartsWith('~')) {
				throw new GeneratorException($"{name}: group name '{group}' may not start with '~' (reserved)");
			}
		}
	}
}
