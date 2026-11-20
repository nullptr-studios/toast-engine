using System;
using System.IO;

namespace editor.Install;

internal static class AppPaths {
	public static string Root => Path.Combine(DataHome, "nullptr-studios");
	public static string EngineDir => Path.Combine(Root, "toast-engine");

	public static string LegacyEngineDir =>
		Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ToastEngine");

	private static string DataHome {
		get {
			if (OperatingSystem.IsWindows()) return Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
			return Environment.GetEnvironmentVariable("XDG_DATA_HOME") is { Length: > 0 } data
				? data
				: Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".local", "share");
		}
	}
}
