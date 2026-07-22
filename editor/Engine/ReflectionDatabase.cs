using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using editor.Assets;

namespace editor.Engine;

public record ParentInfo(
	[property: JsonPropertyName("name")] string Name,
	[property: JsonPropertyName("namespace")]
	string? Namespace
);

public record FieldInfo(
	[property: JsonPropertyName("name")] string Name,
	[property: JsonPropertyName("typename")]
	string TypeName,
	[property: JsonPropertyName("field_type")]
	string FieldType,
	[property: JsonPropertyName("is_array")]
	bool IsArray,
	[property: JsonPropertyName("attributes")]
	JsonElement Attributes,
	[property: JsonPropertyName("default")]
	string? Default
);

public record SubgroupInfo(
	[property: JsonPropertyName("name")] string Name,
	[property: JsonPropertyName("fields")] FieldInfo[] Fields
);

public record GroupInfo(
	[property: JsonPropertyName("name")] string Name,
	[property: JsonPropertyName("subgroups")]
	SubgroupInfo[] Subgroups,
	[property: JsonPropertyName("fields")] FieldInfo[] Fields
);

public record ParameterInfo(
	[property: JsonPropertyName("name")] string Name,
	[property: JsonPropertyName("type")] string Type,
	[property: JsonPropertyName("default")]
	string? Default
);

public record FunctionInfo(
	[property: JsonPropertyName("name")] string Name,
	[property: JsonPropertyName("return_type")]
	string ReturnType,
	[property: JsonPropertyName("parameters")]
	ParameterInfo[] Parameters,
	[property: JsonPropertyName("attributes")]
	JsonElement Attributes
);

public record NodeInfo(
	[property: JsonPropertyName("name")] string Name,
	[property: JsonPropertyName("namespace")]
	string? Namespace,
	[property: JsonPropertyName("parent")] ParentInfo? Parent,
	[property: JsonPropertyName("attributes")]
	JsonElement Attributes,
	[property: JsonPropertyName("tick_functions")]
	Dictionary<string, bool> Functions,
	[property: JsonPropertyName("groups")] GroupInfo[] Groups,
	[property: JsonPropertyName("global_fields")]
	FieldInfo[] GlobalFields,
	[property: JsonPropertyName("methods")]
	FunctionInfo[] Methods,
	[property: JsonPropertyName("source_file")]
	string SourceFile
);

public class NodeTreeItem(string name, NodeInfo info) {
	public string Name { get; } = name;
	public NodeInfo Info { get; } = info;
	public List<NodeTreeItem> Children { get; } = [];
}

/// <summary>Engine + game reflection JSON merged into a node tree</summary>
public static class ReflectionDatabase {
	private static readonly string[] m_reflectionPaths = ["cache://engine_reflect.json", "cache://game_reflect.json"];
	public static Dictionary<string, NodeInfo>? Nodes;
	public static NodeTreeItem? NodeTree;

	public static void Update() {
		var enginePath = ProjectContext.Resolve(m_reflectionPaths[0]);
		var projectPath = ProjectContext.Resolve(m_reflectionPaths[1]);
		if (!File.Exists(enginePath) || !File.Exists(projectPath)) return;

		var engine = JsonSerializer.Deserialize<NodeInfo[]>(File.ReadAllText(enginePath));
		var project = JsonSerializer.Deserialize<NodeInfo[]>(File.ReadAllText(projectPath));
		if (engine is null || project is null) return;

		Nodes = engine.Concat(project).ToDictionary(n => n.Name);

		var treeItems = Nodes.ToDictionary(kv => kv.Key, kv => new NodeTreeItem(kv.Key, kv.Value));

		foreach (var (name, info) in Nodes)
			if (info.Parent is not null && treeItems.TryGetValue(info.Parent.Name, out var parentItem))
				parentItem.Children.Add(treeItems[name]);

		NodeTree = treeItems.GetValueOrDefault("Node");
	}

	// types are namespaced ("toast::Camera") but Nodes is keyed by bare name ("Camera")
	private static string Bare(string typeName) {
		var i = typeName.LastIndexOf(':');
		return i >= 0 ? typeName[(i + 1)..] : typeName;
	}

	public static bool IsTypeOrSubtypeOf(string? typeName, string? baseTypeName) {
		if (Nodes is null || string.IsNullOrEmpty(typeName) || string.IsNullOrEmpty(baseTypeName)) return false;

		var target = Bare(baseTypeName);
		var current = Bare(typeName);

		while (true) {
			if (current == target) return true;
			if (!Nodes.TryGetValue(current, out var info) || info.Parent is null) return false;
			current = Bare(info.Parent.Name);
		}
	}

	// attributes serialize as { "Name": ["x"], "ReadOnly": [], ... }
	public static bool HasAttr(JsonElement attrs, string name) {
		return attrs.ValueKind switch {
			JsonValueKind.Object => attrs.TryGetProperty(name, out _),
			JsonValueKind.Array => attrs.EnumerateArray()
				.Any(e => e.ValueKind == JsonValueKind.String && e.GetString() == name),
			_ => false
		};
	}

	// first string argument of an attribute
	public static string? GetAttr(JsonElement attrs, string name) {
		var args = GetAttrArgs(attrs, name);
		return args.Length > 0 ? args[0] : null;
	}

	// all string arguments of an attribute
	public static string[] GetAttrArgs(JsonElement attrs, string name) {
		if (attrs.ValueKind != JsonValueKind.Object || !attrs.TryGetProperty(name, out var v)) return [];
		if (v.ValueKind != JsonValueKind.Array) return [];
		return v.EnumerateArray()
			.Where(e => e.ValueKind == JsonValueKind.String)
			.Select(e => e.GetString()!)
			.ToArray();
	}

	// class Color attribute is inherited
	public static string ResolveColor(string? typeName) {
		if (Nodes is not null && !string.IsNullOrEmpty(typeName)) {
			var current = Bare(typeName);
			while (Nodes.TryGetValue(current, out var info)) {
				var color = GetAttr(info.Attributes, "Color");
				if (!string.IsNullOrEmpty(color)) return color;
				if (info.Parent is null) break;
				current = Bare(info.Parent.Name);
			}
		}

		return "TextMuted";
	}

	// class Icon attribute is inherited
	public static string ResolveIcon(string? typeName) {
		if (Nodes is not null && !string.IsNullOrEmpty(typeName)) {
			var current = Bare(typeName);
			while (Nodes.TryGetValue(current, out var info)) {
				var icon = GetAttr(info.Attributes, "Icon");
				if (!string.IsNullOrEmpty(icon)) return icon;
				if (info.Parent is null) break;
				current = Bare(info.Parent.Name);
			}
		}

		return "Circle";
	}
}
