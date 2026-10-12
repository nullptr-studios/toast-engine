using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using editor.Assets;

namespace editor.Workspace;

public sealed class InspectorState {
	private readonly Dictionary<string, bool> m_collapsed;
	private readonly Dictionary<string, int> m_numbers;
	private readonly string? m_path;

	private InspectorState(string? path, Dictionary<string, bool> collapsed, Dictionary<string, int> numbers) {
		m_path = path;
		m_collapsed = collapsed;
		m_numbers = numbers;
	}

	public static InspectorState Load(string uid) {
		var path = ProjectContext.Resolve($"cache://tools/inspector/{uid}.json");
		var collapsed = new Dictionary<string, bool>();
		var numbers = new Dictionary<string, int>();
		try {
			if (File.Exists(path)) {
				var data = JsonSerializer.Deserialize<Dictionary<string, JsonElement>>(File.ReadAllText(path));
				foreach (var (key, value) in data ?? []) {
					if (value.ValueKind is JsonValueKind.True or JsonValueKind.False) collapsed[key] = value.GetBoolean();
					else if (value.ValueKind == JsonValueKind.Number && value.TryGetInt32(out var number)) numbers[key] = number;
				}
			}
		} catch {
			// corrupt cache -> fall back to defaults
		}

		return new InspectorState(path, collapsed, numbers);
	}

	public static InspectorState CreateTransient() {
		return new InspectorState(null, new Dictionary<string, bool>(), new Dictionary<string, int>());
	}

	public bool Get(string key, bool defaultCollapsed) {
		return m_collapsed.TryGetValue(key, out var v) ? v : defaultCollapsed;
	}

	public void Set(string key, bool collapsed) {
		m_collapsed[key] = collapsed;
		Save();
	}

	public int GetInt(string key, int fallback) {
		return m_numbers.TryGetValue(key, out var v) ? v : fallback;
	}

	public void SetInt(string key, int value) {
		m_numbers[key] = value;
		Save();
	}

	private void Save() {
		if (m_path is null) return;
		try {
			var all = new Dictionary<string, object>();
			foreach (var (key, value) in m_collapsed) all[key] = value;
			foreach (var (key, value) in m_numbers) all[key] = value;
			Directory.CreateDirectory(Path.GetDirectoryName(m_path)!);
			File.WriteAllText(m_path, JsonSerializer.Serialize(all));
		} catch {
			// best-effort persistence; ignore IO errors
		}
	}
}
