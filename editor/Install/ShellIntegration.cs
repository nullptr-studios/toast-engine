//
// ShellIntegration.cs by Xein
//

using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace editor.Install;

public static class ShellIntegration {
	private const string ProgId = "ToastEngine.Project";
	private const string BlockStart = "# >>> toast-engine >>>";
	private const string BlockEnd = "# <<< toast-engine <<<";

	public static void Apply() {
		try {
			Environment.SetEnvironmentVariable("TOAST_PATH", BuildInfo.ToastPath);
			if (OperatingSystem.IsWindows()) ApplyWindows();
			else if (OperatingSystem.IsLinux()) ApplyLinux();
		} catch (Exception e) {
			Trace.WriteLine($"[ShellIntegration] {e}");
		}
	}

	[DllImport("shell32.dll")]
	private static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);

	private static void ApplyWindows() {
		if (!OperatingSystem.IsWindows()) return;

		// Writes HKCU\Environment
		if (Environment.GetEnvironmentVariable("TOAST_PATH", EnvironmentVariableTarget.User) != BuildInfo.ToastPath)
			Environment.SetEnvironmentVariable("TOAST_PATH", BuildInfo.ToastPath, EnvironmentVariableTarget.User);

		var exe = BuildInfo.EditorExe;
		var changed = false;
		changed |= SetDefault(@"Software\Classes\.toast", ProgId);
		changed |= SetDefault($@"Software\Classes\{ProgId}", "Toast Engine Project");
		changed |= SetDefault($@"Software\Classes\{ProgId}\DefaultIcon", $"\"{exe}\",0");
		changed |= SetDefault($@"Software\Classes\{ProgId}\shell\open\command", $"\"{exe}\" \"%1\"");

		// SHCNE_ASSOCCHANGED, SHCNF_IDLIST
		if (changed) SHChangeNotify(0x08000000, 0, IntPtr.Zero, IntPtr.Zero);
	}

	private static bool SetDefault(string subKey, string value) {
		if (!OperatingSystem.IsWindows()) return false;
		using var key = Registry.CurrentUser.CreateSubKey(subKey);
		if (key.GetValue(null) as string == value) return false;
		key.SetValue(null, value);
		return true;
	}

	private static void ApplyLinux() {
		var home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
		var configHome = Environment.GetEnvironmentVariable("XDG_CONFIG_HOME") is { Length: > 0 } c
			? c : Path.Combine(home, ".config");
		var dataHome = Environment.GetEnvironmentVariable("XDG_DATA_HOME") is { Length: > 0 } d
			? d : Path.Combine(home, ".local", "share");
		var toastPath = BuildInfo.ToastPath;

		// systemd user sessions
		WriteIfChanged(Path.Combine(configHome, "environment.d", "50-toast-engine.conf"), $"TOAST_PATH={toastPath}\n");

		// Shells
		var block = $"{BlockStart}\nexport TOAST_PATH=\"{toastPath}\"\n{BlockEnd}\n";
		foreach (var rc in new[] { ".profile", ".bashrc", ".zshenv" }) {
			var path = Path.Combine(home, rc);
			if (rc != ".profile" && !File.Exists(path)) continue;
			ReplaceBlock(path, block);
		}

		// .toast
		var exe = BuildInfo.EditorExe;
		var mimeChanged = WriteIfChanged(Path.Combine(dataHome, "mime", "packages", "toast-engine.xml"),
			"""
			<?xml version="1.0" encoding="UTF-8"?>
			<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
			  <mime-type type="application/x-toast-project">
			    <comment>Toast Engine Project</comment>
			    <glob pattern="*.toast"/>
			  </mime-type>
			</mime-info>
			""" + "\n");

		var desktopChanged = WriteIfChanged(Path.Combine(dataHome, "applications", "toast-engine.desktop"),
			$"[Desktop Entry]\nType=Application\nName=Toast Engine\nExec=\"{exe}\" %f\n" +
			"MimeType=application/x-toast-project;\nCategories=Development;\nTerminal=false\n");

		if (mimeChanged) Run("update-mime-database", $"\"{Path.Combine(dataHome, "mime")}\"");
		if (desktopChanged) {
			Run("update-desktop-database", $"\"{Path.Combine(dataHome, "applications")}\"");
			Run("xdg-mime", "default toast-engine.desktop application/x-toast-project");
		}
	}

	private static bool WriteIfChanged(string path, string content) {
		if (File.Exists(path) && File.ReadAllText(path) == content) return false;
		Directory.CreateDirectory(Path.GetDirectoryName(path)!);
		File.WriteAllText(path, content, new UTF8Encoding(false));
		return true;
	}

	private static void ReplaceBlock(string path, string block) {
		var text = File.Exists(path) ? File.ReadAllText(path) : "";
		var pattern = new Regex($@"{Regex.Escape(BlockStart)}.*?{Regex.Escape(BlockEnd)}\r?\n?", RegexOptions.Singleline);
		var updated = pattern.IsMatch(text)
			? pattern.Replace(text, block.Replace("$", "$$"), 1)
			: text + (text.Length == 0 || text.EndsWith('\n') ? "" : "\n") + block;
		if (updated != text) File.WriteAllText(path, updated, new UTF8Encoding(false));
	}

	private static void Run(string file, string args) {
		try {
			using var p = Process.Start(new ProcessStartInfo(file, args) {
				UseShellExecute = false, CreateNoWindow = true,
				RedirectStandardOutput = true, RedirectStandardError = true
			});
			p?.WaitForExit(5000);
		} catch {
			// Tool not installed
		}
	}
}
