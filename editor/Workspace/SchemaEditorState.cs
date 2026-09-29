using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using editor.Assets;

namespace editor.Workspace;

public sealed class SchemaEditorState {
	[JsonPropertyName("version")] public int Version { get; set; } = 1;
	[JsonPropertyName("advancedView")] public bool AdvancedView { get; set; }
	[JsonPropertyName("selectedTab")] public string SelectedTab { get; set; } = "fields";

	private static SchemaEditorState? s_instance;

	public static SchemaEditorState Load() {
		if (s_instance != null) return s_instance;

		SchemaEditorState? data = null;
		try {
			var path = ProjectContext.Resolve("cache://tools/schema_editor.json");
			if (File.Exists(path))
				data = JsonSerializer.Deserialize<SchemaEditorState>(File.ReadAllText(path));
		} catch {
			// corrupt cache -> fall back to defaults
		}

		s_instance = data is { Version: 1 } ? data : new SchemaEditorState();
		return s_instance;
	}

	public static void Reset() {
		s_instance = null;
	}

	public void Save() {
		try {
			var path = ProjectContext.Resolve("cache://tools/schema_editor.json");
			Directory.CreateDirectory(Path.GetDirectoryName(path)!);
			var temp = path + ".tmp." + System.Guid.NewGuid().ToString("N");
			File.WriteAllText(temp, JsonSerializer.Serialize(this));
			File.Move(temp, path, true);
		} catch {
			// just give up bro
		}
	}
}
