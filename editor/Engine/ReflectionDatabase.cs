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

public record NodeInfo(
	[property: JsonPropertyName("name")] string Name,
	[property: JsonPropertyName("namespace")]
	string? Namespace,
	[property: JsonPropertyName("parent")] ParentInfo? Parent,
	[property: JsonPropertyName("attributes")]
	JsonElement Attributes,
	[property: JsonPropertyName("functions")]
	Dictionary<string, bool> Functions,
	[property: JsonPropertyName("groups")] GroupInfo[] Groups,
	[property: JsonPropertyName("global_fields")]
	FieldInfo[] GlobalFields,
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
}
