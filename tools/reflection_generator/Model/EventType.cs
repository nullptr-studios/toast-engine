namespace ReflectionGenerator.Model;

internal sealed record EventField(string Name, string TypeName, string? LuaType, bool Readable);

internal sealed record EventType(
	string Name,
	string? Namespace,
	IReadOnlyList<EventField> Fields,
	string SourceFile,
	bool ConstructorCompatible,
	bool Supported,
	bool Sendable,
	string? SkipReason)
{
	public string QualifiedName => Strings.Qualify(Namespace, Name);
}
