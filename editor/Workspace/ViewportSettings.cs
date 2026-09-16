using System;
using System.IO;
using editor.Assets;
using Tomlyn;
using Tomlyn.Serialization;

namespace editor.Workspace;

internal readonly record struct ViewportSettings(CameraMode Mode, double Speed);

internal static class ViewportSettingsStore {
	private const double DefaultSpeed = 5.0;

	public static ViewportSettings Load(string rootUid, bool defaultOrbit) {
		var fallback = new ViewportSettings(defaultOrbit ? CameraMode.Orbit : CameraMode.Free, DefaultSpeed);
		var path = PathFor(rootUid);
		try {
			if (!File.Exists(path)) return fallback;
			var dto = TomlSerializer.Deserialize<ViewportSettingsDto>(File.ReadAllText(path));
			if (dto is null) return fallback;
			if (!double.IsFinite(dto.Speed) || dto.Speed <= 0) return fallback;
			if (dto.Mode.Equals("orbit", StringComparison.OrdinalIgnoreCase))
				return new ViewportSettings(CameraMode.Orbit, dto.Speed);
			if (dto.Mode.Equals("free", StringComparison.OrdinalIgnoreCase))
				return new ViewportSettings(CameraMode.Free, dto.Speed);
			return fallback;
		} catch {
			return fallback;
		}
	}

	public static void Save(string rootUid, CameraMode mode, double speed) {
		var path = PathFor(rootUid);
		var temp = path + ".tmp";
		try {
			Directory.CreateDirectory(Path.GetDirectoryName(path)!);
			var dto = new ViewportSettingsDto {
				Mode = mode == CameraMode.Orbit ? "orbit" : "free",
				Speed = speed
			};
			File.WriteAllText(temp, TomlSerializer.Serialize(dto));
			File.Move(temp, path, true);
		} catch {
			try {
				if (File.Exists(temp)) File.Delete(temp);
			} catch {
				// best-effort cache cleanup
			}
		}
	}

	private static string PathFor(string rootUid) {
		return ProjectContext.Resolve($"cache://tools/viewport/{rootUid}.toml");
	}

	private sealed class ViewportSettingsDto {
		[TomlPropertyName("mode")] public string Mode { get; set; } = "free";
		[TomlPropertyName("speed")] public double Speed { get; set; } = DefaultSpeed;
	}
}
