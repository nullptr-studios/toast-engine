using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using editor.Assets;

namespace editor.Workspace;

public sealed class LabelWidthState {
	private readonly string m_path;
	private readonly Dictionary<string, double> m_widths;
	private static LabelWidthState? s_project;
	private LabelWidthState(string path, Dictionary<string, double> widths) { m_path = path; m_widths = widths; }
	public static LabelWidthState Load(string path) {
		try {
			return new LabelWidthState(path, File.Exists(path)
				? JsonSerializer.Deserialize<Dictionary<string, double>>(File.ReadAllText(path)) ?? [] : []);
		} catch { return new LabelWidthState(path, []); }
	}
	public static LabelWidthState? ForProject() {
		if (!ProjectContext.IsInitialized) return null;
		var path = ProjectContext.Resolve("cache://tools/label_widths.json");
		if (s_project?.m_path != path) s_project = Load(path);
		return s_project;
	}
	public double? Get(string key) => m_widths.TryGetValue(key, out var width) && double.IsFinite(width) && width >= 0 ? width : null;
	public void Set(string key, double width) {
		if (!double.IsFinite(width) || width < 0) return;
		m_widths[key] = width;
		try {
			Directory.CreateDirectory(Path.GetDirectoryName(m_path)!);
			var temp = m_path + ".tmp." + Guid.NewGuid().ToString("N");
			File.WriteAllText(temp, JsonSerializer.Serialize(m_widths));
			File.Move(temp, m_path, true);
		} catch { /* Best-effort editor state. */ }
	}
}
