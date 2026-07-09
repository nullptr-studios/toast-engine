using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace editor.Assets.Importers;

public class ImportTreeState {
	private static readonly JsonSerializerOptions s_pretty = new() { WriteIndented = true };

	private readonly JsonObject m_data;
	private readonly string m_path;

	private ImportTreeState(string path, JsonObject data) {
		m_path = path;
		m_data = data;
	}

	public static ImportTreeState Load() {
		var path = ProjectContext.Resolve("cache://import_tree_state.json");
		if (File.Exists(path)) {
			try {
				if (JsonNode.Parse(File.ReadAllText(path)) is JsonObject obj)
					return new ImportTreeState(path, obj);
			} catch {
				// corrupt file
			}
		}

		return new ImportTreeState(path, new JsonObject());
	}

	public bool? GetFileSelected(string fullPath) {
		return TryGetBool("file:" + fullPath);
	}

	public bool GetFolderCollapsed(string fullPath) {
		return TryGetBool("collapsed:" + fullPath) ?? false;
	}

	public bool GetCardCollapsed(string cardName) {
		return TryGetBool("card:" + cardName) ?? false;
	}

	public void SetFileSelected(string fullPath, bool selected) {
		Set("file:" + fullPath, selected);
	}

	public void SetFolderCollapsed(string fullPath, bool collapsed) {
		Set("collapsed:" + fullPath, collapsed);
	}

	public void SetCardCollapsed(string cardName, bool collapsed) {
		Set("card:" + cardName, collapsed);
	}

	private bool? TryGetBool(string key) {
		if (m_data[key] is JsonValue v && v.TryGetValue<bool>(out var val))
			return val;
		return null;
	}

	private void Set(string key, bool value) {
		m_data[key] = value;
		Save();
	}

	private void Save() {
		try {
			File.WriteAllText(m_path, m_data.ToJsonString(s_pretty));
		} catch {
			// ignore write failures
		}
	}
}
