//
// LogFilterState.cs by Xein
// 28 Jul 2026
//

using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using editor.Assets;

namespace editor.Logger;

public sealed class LogFilterState {
	private static readonly JsonSerializerOptions s_options = new() { PropertyNameCaseInsensitive = true };
	private readonly Model m_model;

	private readonly string m_path;

	private LogFilterState(string path, Model model) {
		m_path = path;
		m_model = model;
	}

	public static LogFilterState Load() {
		if (!ProjectContext.IsInitialized) return new LogFilterState("", new Model());

		var path = ProjectContext.Resolve("cache://logs/filters.json");
		Model? data = null;
		try {
			if (File.Exists(path))
				data = JsonSerializer.Deserialize<Model>(File.ReadAllText(path), s_options);
		} catch {
			// corrupt cache, use defaults
		}

		return new LogFilterState(path, data ?? new Model());
	}

	public bool GetSeverity(string key, bool defaultEnabled) {
		return m_model.Severities.TryGetValue(key, out var v) ? v : defaultEnabled;
	}

	public void SetSeverity(string key, bool enabled) {
		m_model.Severities[key] = enabled;
		Save();
	}

	public bool IsSinkSeverityEnabled(string sink, string severityKey) {
		return !m_model.SinkSeverityOverrides.TryGetValue(sink, out var disabled) || !disabled.Contains(severityKey);
	}

	public void SetSinkSeverity(string sink, string severityKey, bool enabled) {
		SetSinkSeverityCore(sink, severityKey, enabled);
		Save();
	}

	public void SetSinkSeverities(string sink, IEnumerable<(string SeverityKey, bool Enabled)> values) {
		foreach (var (severityKey, enabled) in values) SetSinkSeverityCore(sink, severityKey, enabled);
		Save();
	}

	public void ResetAllSinkSeverities() {
		m_model.SinkSeverityOverrides.Clear();
		Save();
	}

	private void SetSinkSeverityCore(string sink, string severityKey, bool enabled) {
		if (!m_model.SinkSeverityOverrides.TryGetValue(sink, out var disabled)) {
			if (enabled) return; // already enabled by default
			disabled = [];
			m_model.SinkSeverityOverrides[sink] = disabled;
		}

		if (enabled) disabled.Remove(severityKey);
		else if (!disabled.Contains(severityKey)) disabled.Add(severityKey);

		if (disabled.Count == 0) m_model.SinkSeverityOverrides.Remove(sink);
	}

	private void Save() {
		if (string.IsNullOrEmpty(m_path)) return;
		try {
			Directory.CreateDirectory(Path.GetDirectoryName(m_path)!);
			File.WriteAllText(m_path, JsonSerializer.Serialize(m_model, s_options));
		} catch {
			// best-effort
		}
	}

	private sealed class Model {
		public int Version { get; set; } = 2;
		public Dictionary<string, bool> Severities { get; set; } = new();
		public Dictionary<string, List<string>> SinkSeverityOverrides { get; set; } = new();
	}
}
