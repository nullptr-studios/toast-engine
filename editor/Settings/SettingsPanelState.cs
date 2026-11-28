//
// SettingsPanelState.cs by Xein
// 24 Nov 2026
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using editor.Assets;

namespace editor.Settings;

public sealed class SettingsPanelState {
	private static SettingsPanelState? s_current;
	private static string s_currentPath = string.Empty;

	private string m_path = string.Empty;

	public double WindowWidth { get; set; } = 860;
	public double WindowHeight { get; set; } = 700;
	public double NavWidth { get; set; } = 190;

	public Dictionary<string, bool> Toggles { get; set; } = [];

	public static SettingsPanelState Current {
		get {
			var path = ProjectContext.IsInitialized ? ProjectContext.Resolve("cache://tools/settings_panel.json") : string.Empty;
			if (s_current is not null && s_currentPath == path) return s_current;
			s_currentPath = path;
			s_current = Load(path);
			return s_current;
		}
	}

	private static SettingsPanelState Load(string path) {
		try {
			if (path.Length > 0 && File.Exists(path)) {
				var loaded = JsonSerializer.Deserialize<SettingsPanelState>(File.ReadAllText(path));
				if (loaded is not null) {
					loaded.m_path = path;
					loaded.Toggles ??= [];
					loaded.WindowWidth = Math.Clamp(loaded.WindowWidth, 600, 4000);
					loaded.WindowHeight = Math.Clamp(loaded.WindowHeight, 400, 4000);
					loaded.NavWidth = Math.Clamp(loaded.NavWidth, 120, 500);
					return loaded;
				}
			}
		} catch {
		}

		return new SettingsPanelState { m_path = path };
	}

	public bool Get(string key, bool fallback) => Toggles.TryGetValue(key, out var value) ? value : fallback;

	public void Set(string key, bool value, bool fallback) {
		if (value == fallback) Toggles.Remove(key);
		else Toggles[key] = value;
		Save();
	}

	public void Save() {
		if (m_path.Length == 0) return;
		try {
			Directory.CreateDirectory(Path.GetDirectoryName(m_path)!);
			var temp = m_path + ".tmp." + Guid.NewGuid().ToString("N");
			File.WriteAllText(temp, JsonSerializer.Serialize(this));
			File.Move(temp, m_path, true);
		} catch {
			// best effort, like the other editor state
		}
	}
}
