using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Serialization;
using ReflectionGenerator.Analysis;
using ReflectionGenerator.Model;
using ReflectionGenerator.Rules;

namespace ReflectionGenerator.Output;

// keys must stay in step with editor/Engine/ReflectionDatabase.cs
internal static class Database
{
	private static readonly JsonSerializerOptions JsonOptions = new() {
		WriteIndented = true,
		PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
		Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
	};

	public static string Serialize(IEnumerable<NodeType> nodes, IEnumerable<EventType> events) =>
		ToJson(new DatabaseDto(nodes.Select(NodeDto.From).ToList(), events.Select(EventDto.From).ToList()));

	public static string SerializeNodes(IEnumerable<NodeType> nodes) =>
		ToJson(nodes.Select(NodeDto.From).ToList());

	private static string ToJson(object value) => JsonSerializer.Serialize(value, JsonOptions).Replace("\r\n", "\n");

	private sealed record DatabaseDto(List<NodeDto> Nodes, List<EventDto> Events);

	private sealed record ParentDto(string Name, string? Namespace);

	private sealed record AttributeDto(string Name, List<string> Args);

	private sealed record ParameterDto(string Name, string Type, string? Default);

	private sealed record FieldDto(
		string Name,
		[property: JsonPropertyName("typename")] string TypeName,
		string FieldType,
		bool IsArray,
		SortedDictionary<string, string[]> Attributes,
		string? Default)
	{
		public static FieldDto From(Field f) =>
			new(f.Name, f.TypeName, f.Kind.Name(), f.IsArray, f.Attributes.ByName(), f.Default);
	}

	private sealed record MethodDto(
		string Name,
		string ReturnType,
		List<ParameterDto> Parameters,
		SortedDictionary<string, string[]> Attributes,
		bool IsConst)
	{
		public static MethodDto From(Method m) => new(
			m.Name,
			m.ReturnType,
			m.Parameters.Select(p => new ParameterDto(p.Name, p.Type, p.Default)).ToList(),
			m.Attributes.ByName(),
			m.IsConst);
	}

	private sealed record SignalDto(
		string Name,
		[property: JsonPropertyName("typename")] string TypeName,
		List<string> Arguments,
		List<AttributeDto> AttrsList,
		SortedDictionary<string, string[]> Attributes)
	{
		public static SignalDto From(Signal s) => new(
			s.Name,
			s.TypeName,
			s.Arguments.ToList(),
			s.Attributes.Select(a => new AttributeDto(a.Name, a.Args.ToList())).ToList(),
			s.Attributes.ByName());
	}

	private sealed record SubgroupDto(string Name, List<FieldDto> Fields);

	private sealed record GroupDto(string Name, List<SubgroupDto> Subgroups, List<FieldDto> Fields);

	private sealed record NodeDto(
		string Name,
		string? Namespace,
		ParentDto? Parent,
		SortedDictionary<string, string[]> Attributes,
		List<string> Functions,
		List<MethodDto> Methods,
		List<FieldDto> Fields,
		List<SignalDto> Signals,
		string SourceFile,
		List<GroupDto> Groups,
		List<FieldDto> GlobalFields,
		Dictionary<string, bool> TickFunctions,
		bool IsInterface)
	{
		public static NodeDto From(NodeType node)
		{
			var layout = FieldLayout.From(node.Fields);
			return new NodeDto(
				node.Name,
				node.Namespace,
				node.Parent is { } p ? new ParentDto(p.Name, p.Namespace) : null,
				node.Attributes.ByName(),
				node.TickFunctions.ToList(),
				node.Methods.Select(MethodDto.From).ToList(),
				node.Fields.Select(FieldDto.From).ToList(),
				node.Signals.Select(SignalDto.From).ToList(),
				node.SourceFile,
				layout.Groups.Select(g => new GroupDto(
					g.Name,
					g.Subgroups.Select(s => new SubgroupDto(s.Name, s.Fields.Select(FieldDto.From).ToList())).ToList(),
					g.Fields.Select(FieldDto.From).ToList())).ToList(),
				layout.GlobalFields.Select(FieldDto.From).ToList(),
				Rules.TickFunctions.All.ToDictionary(t => t.Flag, t => node.TickFunctions.Contains(t.Method)),
				node.IsInterface);
		}
	}

	private sealed record EventFieldDto(
		string Name,
		[property: JsonPropertyName("typename")] string TypeName,
		string? LuaType,
		bool Readable);

	private sealed record EventDto(
		string Name,
		string? Namespace,
		List<EventFieldDto> Fields,
		string SourceFile,
		bool ConstructorCompatible,
		bool Supported,
		bool Sendable,
		string? SkipReason)
	{
		public static EventDto From(EventType e) => new(
			e.Name,
			e.Namespace,
			e.Fields.Select(f => new EventFieldDto(f.Name, f.TypeName, f.LuaType, f.Readable)).ToList(),
			e.SourceFile,
			e.ConstructorCompatible,
			e.Supported,
			e.Sendable,
			e.SkipReason);
	}
}
