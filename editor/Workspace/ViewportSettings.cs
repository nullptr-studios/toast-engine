using System;
using System.IO;
using editor.Assets;
using Tomlyn;
using Tomlyn.Serialization;

namespace editor.Workspace;

internal readonly record struct ViewportSettings(CameraMode Mode, double Speed, bool PlayInGameCamera);

internal static class ViewportSettingsStore {
	private const double DefaultSpeed = 5.0;

	public static ViewportSettings Load(string key, bool defaultOrbit) {
		var fallback = new ViewportSettings(defaultOrbit ? CameraMode.Orbit : CameraMode.Free, DefaultSpeed, true);
		var path = PathFor(key);
		try {
			if (!File.Exists(path)) return fallback;
			var dto = TomlSerializer.Deserialize<ViewportSettingsDto>(File.ReadAllText(path));
			if (dto is null) return fallback;
			var speed = double.IsFinite(dto.Speed) && dto.Speed > 0 ? dto.Speed : DefaultSpeed;
			var mode = dto.Mode.ToLowerInvariant() switch {
				"orbit" => CameraMode.Orbit,
				"free" => CameraMode.Free,
				_ => fallback.Mode
			};
			return new ViewportSettings(mode, speed, dto.PlayInGameCamera);
		} catch {
			return fallback;
		}
	}

	public static void Save(string key, ViewportSettings settings) {
		var path = PathFor(key);
		var temp = path + ".tmp";
		try {
			Directory.CreateDirectory(Path.GetDirectoryName(path)!);
			var dto = new ViewportSettingsDto {
				Mode = settings.Mode == CameraMode.Orbit ? "orbit" : "free",
				Speed = settings.Speed,
				PlayInGameCamera = settings.PlayInGameCamera
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

	private static string PathFor(string key) {
		return ProjectContext.Resolve($"cache://tools/viewport/{key}.toml");
	}

	private sealed class ViewportSettingsDto {
		[TomlPropertyName("mode")] public string Mode { get; set; } = "free";
		[TomlPropertyName("speed")] public double Speed { get; set; } = DefaultSpeed;
		[TomlPropertyName("play_in_game_camera")] public bool PlayInGameCamera { get; set; } = true;
	}
}
