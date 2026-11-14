//
// BuildInfo.cs
// Where this editor is installed and which release it came from
//

using System;
using System.IO;

namespace editor.Install;

public static class BuildInfo {
	public static string InstallRoot { get; } =
		Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, ".."));

	public static string ToastPath { get; } =
		Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "toast_engine"));

	public static string EditorExe => Environment.ProcessPath ?? Path.Combine(AppContext.BaseDirectory, "editor");

	public static Version? Version { get; } = ReadVersion();

	public static string UpdateRepo =>
		Environment.GetEnvironmentVariable("TOAST_UPDATE_REPO") is { Length: > 0 } r ? r : "nullptr-studios/toast-engine";

	private static Version? ReadVersion() {
		try {
			var file = Path.Combine(InstallRoot, "version.txt");
			if (!File.Exists(file)) return null;
			return System.Version.TryParse(File.ReadAllText(file).Trim().TrimStart('v'), out var v) ? v : null;
		} catch {
			return null;
		}
	}
}
