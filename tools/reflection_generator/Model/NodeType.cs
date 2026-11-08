namespace ReflectionGenerator.Model;

internal sealed record ParentRef(string Name, string? Namespace);

internal sealed record Parameter(string Name, string Type, string? Default);

internal sealed record Field(
	string Name,
	string TypeName,
	FieldKind Kind,
	bool IsArray,
	AttributeList Attributes,
	string? Default);

internal sealed record Method(
	string Name,
	string ReturnType,
	IReadOnlyList<Parameter> Parameters,
	bool IsConst,
	AttributeList Attributes);

internal sealed record Signal(
	string Name,
	string TypeName,
	IReadOnlyList<string> Arguments,
	AttributeList Attributes);

internal sealed record NodeType(
	string Name,
	string? Namespace,
	ParentRef? Parent,
	AttributeList Attributes,
	IReadOnlyList<string> TickFunctions,
	IReadOnlyList<Method> Methods,
	IReadOnlyList<Field> Fields,
	IReadOnlyList<Signal> Signals,
	string SourceFile)
{
	public string QualifiedName => Strings.Qualify(Namespace, Name);

	public bool IsInterface => Attributes.Has("Interface");
}
