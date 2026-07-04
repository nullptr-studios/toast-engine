//
// Haptic.cs by Xein
// 04 Jul 2026
//

using System;
using System.IO;
using Tomlyn;
using Tomlyn.Model;

namespace editor.Assets;

public enum HapticMode {
	Standard,
	Curve,
	AdaptiveTrigger,
	AudioHaptic
}

public enum HapticChannels {
	Single,
	Dual
}

public sealed class Haptic {
	public string SchemaUid { get; set; } = "";
	public HapticMode Mode { get; set; } = HapticMode.Standard;
	public int Priority { get; set; }
	public int DurationMs { get; set; } = 200;

	// standard mode
	public float Left { get; set; } = 0.5f;
	public float Right { get; set; } = 0.5f;

	// curve mode
	public HapticChannels Channels { get; set; } = HapticChannels.Single;
	public float Pan { get; set; }
	public float Multiplier { get; set; } = 1f;
	public Curve? Curve { get; set; }
	public Curve? CurveRight { get; set; }

	public static Haptic FromFile(string path) {
		return FromString(File.ReadAllText(path));
	}

	public static Haptic FromString(string toml) {
		var table = TomlSerializer.Deserialize<TomlTable>(toml)
			?? throw new FormatException("Haptic: failed to parse TOML");

		var h = new Haptic {
			SchemaUid = GetString(table, "schema", ""),
			Mode = ParseMode(GetString(table, "mode", "standard")),
			Priority = GetInt(table, "priority", 0),
			DurationMs = GetInt(table, "duration_ms", 200),
			Left = GetFloat(table, "left", 0.5f),
			Right = GetFloat(table, "right", 0.5f),
			Channels = GetString(table, "channels", "single") == "dual"
				? HapticChannels.Dual
				: HapticChannels.Single,
			Pan = GetFloat(table, "pan", 0f),
			Multiplier = GetFloat(table, "multiplier", 1f)
		};

		if (table.TryGetValue("curve", out var c) && c is TomlTable ct)
			h.Curve = Curve.FromToml(ct);
		if (table.TryGetValue("curve_right", out var cr) && cr is TomlTable crt)
			h.CurveRight = Curve.FromToml(crt);

		return h;
	}

	public void Save(string path) {
		File.WriteAllText(path, Serialize());
	}

	public string Serialize() {
		var table = new TomlTable();
		if (SchemaUid.Length > 0)
			table["schema"] = SchemaUid;
		table["mode"] = ModeToString(Mode);
		table["priority"] = (long)Priority;
		table["duration_ms"] = (long)DurationMs;

		switch (Mode) {
			case HapticMode.Standard:
				table["left"] = (double)Left;
				table["right"] = (double)Right;
				break;
			case HapticMode.Curve:
				table["channels"] = Channels == HapticChannels.Dual ? "dual" : "single";
				table["pan"] = (double)Pan;
				table["multiplier"] = (double)Multiplier;
				if (Curve is { } curve)
					table["curve"] = curve.ToToml();
				if (Channels == HapticChannels.Dual && CurveRight is { } curveRight)
					table["curve_right"] = curveRight.ToToml();
				break;
		}

		return TomlSerializer.Serialize(table);
	}

	private static string ModeToString(HapticMode m) {
		return m switch {
			HapticMode.Standard => "standard",
			HapticMode.Curve => "curve",
			HapticMode.AdaptiveTrigger => "adaptive_trigger",
			HapticMode.AudioHaptic => "audio_haptic",
			_ => "standard"
		};
	}

	private static HapticMode ParseMode(string s) {
		return s switch {
			"curve" => HapticMode.Curve,
			"adaptive_trigger" => HapticMode.AdaptiveTrigger,
			"audio_haptic" => HapticMode.AudioHaptic,
			_ => HapticMode.Standard
		};
	}

	private static string GetString(TomlTable t, string key, string fallback) {
		return t.TryGetValue(key, out var v) && v is string s ? s : fallback;
	}

	private static int GetInt(TomlTable t, string key, int fallback) {
		return t.TryGetValue(key, out var v)
			? v switch { long l => (int)l, int i => i, double d => (int)d, _ => fallback }
			: fallback;
	}

	private static float GetFloat(TomlTable t, string key, float fallback) {
		return t.TryGetValue(key, out var v)
			? v switch { double d => (float)d, float f => f, long l => l, int i => i, _ => fallback }
			: fallback;
	}
}
