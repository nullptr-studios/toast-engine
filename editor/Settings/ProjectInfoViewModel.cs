//
// ProjectInfoViewModel.cs by Xein
// 24 Nov 2026
//

using System;
using System.IO;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets;
using Tomlyn;
using Tomlyn.Model;

namespace editor.Settings;

public sealed partial class ProjectInfoViewModel : ObservableObject {
	private string m_path = string.Empty;
	private bool m_loading;

	[ObservableProperty] private int m_major;
	[ObservableProperty] private int m_minor;
	[ObservableProperty] private string m_name = string.Empty;
	[ObservableProperty] private int m_patch;
	[ObservableProperty] private string m_status = string.Empty;

	public bool HasProject => m_path.Length > 0;

	public void Load() {
		m_path = ProjectContext.IsInitialized
			? Directory.EnumerateFiles(ProjectContext.ProjectPath, "*.toast").FirstOrDefault() ?? string.Empty
			: string.Empty;
		OnPropertyChanged(nameof(HasProject));
		if (m_path.Length == 0) return;

		m_loading = true;
		try {
			var root = TomlSerializer.Deserialize<TomlTable>(File.ReadAllText(m_path)) ?? [];
			Name = root.TryGetValue("name", out var name) ? name?.ToString() ?? string.Empty : string.Empty;
			var version = root.TryGetValue("version", out var v) && v is TomlArray array ? array : null;
			Major = Component(version, 0);
			Minor = Component(version, 1);
			Patch = Component(version, 2);
		} catch (Exception e) {
			Status = $"Could not read the project file: {e.Message}";
		} finally {
			m_loading = false;
		}
	}

	private static int Component(TomlArray? array, int index) {
		return array is not null && index < array.Count && array[index] is long number ? (int)Math.Clamp(number, 0, int.MaxValue) : 0;
	}

	partial void OnNameChanged(string value) => Save();
	partial void OnMajorChanged(int value) => Save();
	partial void OnMinorChanged(int value) => Save();
	partial void OnPatchChanged(int value) => Save();

	private void Save() {
		if (m_loading || m_path.Length == 0) return;
		try {
			var root = TomlSerializer.Deserialize<TomlTable>(File.ReadAllText(m_path)) ?? [];
			root["name"] = Name;
			root["version"] = new TomlArray { (long)Major, (long)Minor, (long)Patch };
			File.WriteAllText(m_path, TomlSerializer.Serialize(root));
			Status = string.Empty;
		} catch (Exception e) {
			Status = $"Could not write the project file: {e.Message}";
		}
	}
}
