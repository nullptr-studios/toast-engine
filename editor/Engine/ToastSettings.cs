//
// ToastSettings.cs
// 16 Aug 2026
//

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace editor.Engine;

/// Storage type of a setting, mirroring toast::settings::Type
public enum SettingType {
	Bool = 0,
	Int = 1,
	Float = 2,
	String = 3
}

/// <summary>One setting as the engine describes it, copied out of the native registry</summary>
/// <remarks>
/// A managed copy rather than a view: the native strings are owned by the registry and only valid until the
/// next call that could add a setting, so they are marshalled once during enumeration and never held
/// </remarks>
public sealed class SettingDescriptor {
	public required string Key { get; init; }
	public required string Label { get; init; }
	public required string Category { get; init; }
	public required string Description { get; init; }
	public required SettingType Type { get; init; }
	public double Min { get; init; }
	public double Max { get; init; }
	public double Step { get; init; }
	public IReadOnlyList<string> Options { get; init; } = [];
	public bool RequiresRestart { get; init; }
	public bool Hidden { get; init; }

	/// Equal bounds mean the engine declared none, so the editor shows a number box rather than a slider
	public bool HasRange => Math.Abs(Max - Min) > double.Epsilon;

	/// A non-empty option list turns an integer setting into a combo box indexing it
	public bool IsEnum => Type == SettingType.Int && Options.Count > 0;
}

[StructLayout(LayoutKind.Sequential)]
internal struct ToastSettingDesc {
	public IntPtr key;
	public IntPtr label;
	public IntPtr category;
	public IntPtr description;
	public int type;
	public double min;
	public double max;
	public double step;
	public int optionCount;
	public int requiresRestart;
	public int hidden;
}

/// <summary>
/// The engine's settings registry, enumerated rather than hardcoded
/// </summary>
/// <remarks>
/// Nothing here names an individual setting. A system that declares one in C++ shows up in the settings
/// window with no change on this side - which is the point of the whole arrangement, since the alternative
/// is a C# property, an FFI entry point and a XAML row per knob
/// </remarks>
public static partial class ToastSettings {
	private const string EngineLib = "toast_engine";

	/// <summary>Every declared setting, in the order the engine declared them</summary>
	public static List<SettingDescriptor> Enumerate() {
		var count = toast_settings_count();
		var result = new List<SettingDescriptor>(count);

		for (var i = 0; i < count; i++) {
			if (toast_settings_get_desc(i, out var desc) == 0) continue;

			var options = new List<string>(desc.optionCount);
			for (var o = 0; o < desc.optionCount; o++) {
				var option = toast_settings_get_option(i, o);
				options.Add(option == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUTF8(option) ?? string.Empty);
			}

			result.Add(new SettingDescriptor {
				Key = Marshal.PtrToStringUTF8(desc.key) ?? string.Empty,
				Label = Marshal.PtrToStringUTF8(desc.label) ?? string.Empty,
				Category = Marshal.PtrToStringUTF8(desc.category) ?? string.Empty,
				Description = Marshal.PtrToStringUTF8(desc.description) ?? string.Empty,
				Type = (SettingType)desc.type,
				Min = desc.min,
				Max = desc.max,
				Step = desc.step,
				Options = options,
				RequiresRestart = desc.requiresRestart != 0,
				Hidden = desc.hidden != 0
			});
		}

		return result;
	}

	public static bool GetBool(string key) => toast_settings_get_bool(key) != 0;
	public static long GetInt(string key) => toast_settings_get_int(key);
	public static double GetFloat(string key) => toast_settings_get_float(key);

	public static string GetString(string key) {
		var ptr = toast_settings_get_string(key);
		return ptr == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUTF8(ptr) ?? string.Empty;
	}

	public static void SetBool(string key, bool value) => toast_settings_set_bool(key, value ? 1 : 0);
	public static void SetInt(string key, long value) => toast_settings_set_int(key, value);
	public static void SetFloat(string key, double value) => toast_settings_set_float(key, value);
	public static void SetString(string key, string value) => toast_settings_set_string(key, value);

	public static void Reset(string key) => toast_settings_reset(key);
	public static void ResetAll() => toast_settings_reset_all();

	public static bool Save() => toast_settings_save() != 0;
	public static bool IsDirty => toast_settings_is_dirty() != 0;

	/// Where the active layer writes. The editor writes the project layer, so this is the file that ships
	public static string ActivePath {
		get {
			var ptr = toast_settings_active_path();
			return ptr == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUTF8(ptr) ?? string.Empty;
		}
	}

	[LibraryImport(EngineLib)]
	private static partial int toast_settings_count();

	[LibraryImport(EngineLib)]
	private static partial int toast_settings_get_desc(int index, out ToastSettingDesc desc);

	[LibraryImport(EngineLib)]
	private static partial IntPtr toast_settings_get_option(int index, int option);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial int toast_settings_get_bool(string key);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial long toast_settings_get_int(string key);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial double toast_settings_get_float(string key);

	// IntPtr rather than a marshalled string: the engine hands back a pointer it still owns, and letting the
	// marshaller treat it as an owned string would have the runtime free the engine's buffer
	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial IntPtr toast_settings_get_string(string key);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_settings_set_bool(string key, int value);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_settings_set_int(string key, long value);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_settings_set_float(string key, double value);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_settings_set_string(string key, string value);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_settings_reset(string key);

	[LibraryImport(EngineLib)]
	private static partial void toast_settings_reset_all();

	[LibraryImport(EngineLib)]
	private static partial int toast_settings_save();

	[LibraryImport(EngineLib)]
	private static partial int toast_settings_is_dirty();

	[LibraryImport(EngineLib)]
	private static partial IntPtr toast_settings_active_path();
}

/// <summary>Renderer commands and status - the things that are actions rather than values</summary>
public static partial class ToastRenderer {
	private const string EngineLib = "toast_engine";

	public static void BakeReflectionProbes() => toast_renderer_bake_reflection_probes();
	public static void BakeIrradianceVolumes() => toast_renderer_bake_irradiance_volumes();
	public static uint StaleReflectionProbes => toast_renderer_stale_reflection_probes();
	public static uint StaleIrradianceVolumes => toast_renderer_stale_irradiance_volumes();
	public static uint IrradianceProbeCount => toast_renderer_irradiance_probe_count();
	public static bool SupportsRayTracing => toast_renderer_supports_ray_tracing() != 0;

	[LibraryImport(EngineLib)]
	private static partial void toast_renderer_bake_reflection_probes();

	[LibraryImport(EngineLib)]
	private static partial void toast_renderer_bake_irradiance_volumes();

	[LibraryImport(EngineLib)]
	private static partial uint toast_renderer_stale_reflection_probes();

	[LibraryImport(EngineLib)]
	private static partial uint toast_renderer_stale_irradiance_volumes();

	[LibraryImport(EngineLib)]
	private static partial uint toast_renderer_irradiance_probe_count();

	[LibraryImport(EngineLib)]
	private static partial int toast_renderer_supports_ray_tracing();
}
