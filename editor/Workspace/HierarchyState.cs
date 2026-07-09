using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using editor.Assets;

namespace editor.Workspace;

public sealed class HierarchyState {
	private readonly Dictionary<string, bool> m_collapsed;
	private readonly string m_path;

	private HierarchyState(string path, Dictionary<string, bool> collapsed) {
		m_path = path;
		m_collapsed = collapsed;
	}

	public static HierarchyState Load(string rootUid) {
		var path = ProjectContext.Resolve($"cache://hierarchy/{rootUid}.json");
		Dictionary<string, bool>? data = null;
		try {
			if (File.Exists(path))
				data = JsonSerializer.Deserialize<Dictionary<string, bool>>(File.ReadAllText(path));
		} catch {
			// corrupt cache
		}

		return new HierarchyState(path, data ?? new Dictionary<string, bool>());
	}

	public bool Get(string key, bool defaultCollapsed) {
		return m_collapsed.TryGetValue(key, out var v) ? v : defaultCollapsed;
	}

	public void Set(string key, bool collapsed) {
		// only persist the collapsed entries to keep the file small
		if (collapsed) m_collapsed[key] = true;
		else m_collapsed.Remove(key);
		Save();
	}

	private void Save() {
		try {
			Directory.CreateDirectory(Path.GetDirectoryName(m_path)!);
			File.WriteAllText(m_path, JsonSerializer.Serialize(m_collapsed));
		} catch {
			// ignore IO errors
		}
	}
}
