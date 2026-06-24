using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using editor.Assets;

namespace editor.Workspace;

public sealed class InspectorState {
	private readonly Dictionary<string, bool> m_collapsed;
	private readonly string m_path;

	private InspectorState(string path, Dictionary<string, bool> collapsed) {
		m_path = path;
		m_collapsed = collapsed;
	}

	public static InspectorState Load(string uid) {
		var path = ProjectContext.Resolve($"cache://inspector/{uid}.json");
		Dictionary<string, bool>? data = null;
		try {
			if (File.Exists(path))
				data = JsonSerializer.Deserialize<Dictionary<string, bool>>(File.ReadAllText(path));
		}
		catch {
			// corrupt cache -> fall back to defaults
		}

		return new InspectorState(path, data ?? new Dictionary<string, bool>());
	}

	public bool Get(string key, bool defaultCollapsed) =>
		m_collapsed.TryGetValue(key, out var v) ? v : defaultCollapsed;

	public void Set(string key, bool collapsed) {
		m_collapsed[key] = collapsed;
		Save();
	}

	private void Save() {
		try {
			Directory.CreateDirectory(Path.GetDirectoryName(m_path)!);
			File.WriteAllText(m_path, JsonSerializer.Serialize(m_collapsed));
		}
		catch {
			// best-effort persistence; ignore IO errors
		}
	}
}
