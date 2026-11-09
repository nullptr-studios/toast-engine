//
// Updater.cs by Xein
//

using System;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Text.Json;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Threading;

namespace editor.Install;

public sealed record UpdateInfo(Version Version, string AssetName, string DownloadUrl);

public static class Updater {
	private static readonly HttpClient Http = CreateClient();

	private static HttpClient CreateClient() {
		var c = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
		c.DefaultRequestHeaders.UserAgent.ParseAdd("toast-engine-editor");
		c.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
		return c;
	}

	private static string ExpectedAsset(Version v) =>
		OperatingSystem.IsWindows() ? $"ToastEngine-{v}-setup.exe" : $"toast-engine-{v}-linux-x64.tar.gz";

	public static async Task<UpdateInfo?> CheckAsync() {
		var current = BuildInfo.Version;
		if (current is null) return null;

		try {
			var json = await Http.GetStringAsync($"https://api.github.com/repos/{BuildInfo.UpdateRepo}/releases/latest");
			using var doc = JsonDocument.Parse(json);
			var tag = doc.RootElement.GetProperty("tag_name").GetString()?.TrimStart('v');
			if (!Version.TryParse(tag, out var latest) || latest <= current) return null;

			var wanted = ExpectedAsset(latest);
			foreach (var asset in doc.RootElement.GetProperty("assets").EnumerateArray()) {
				if (asset.GetProperty("name").GetString() == wanted)
					return new UpdateInfo(latest, wanted, asset.GetProperty("browser_download_url").GetString()!);
			}
		} catch (Exception e) {
			Trace.WriteLine($"[Updater] check failed: {e.Message}");
		}

		return null;
	}

	public static async Task ApplyAsync(UpdateInfo info, IProgress<double>? progress = null) {
		var dir = Path.Combine(Path.GetTempPath(), "toast-engine-update", info.Version.ToString());
		Directory.CreateDirectory(dir);
		var file = Path.Combine(dir, info.AssetName);
		await Download(info.DownloadUrl, file, progress);

		if (OperatingSystem.IsWindows()) {
			// The installer closes this editor and relaunches the new one
			Process.Start(new ProcessStartInfo(file, "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS") {
				UseShellExecute = true
			});
		} else {
			ApplyLinux(file, dir);
		}

		Dispatcher.UIThread.Post(() =>
			(Application.Current?.ApplicationLifetime as IClassicDesktopStyleApplicationLifetime)?.Shutdown());
	}

	private static async Task Download(string url, string dest, IProgress<double>? progress) {
		using var response = await Http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
		response.EnsureSuccessStatusCode();
		var total = response.Content.Headers.ContentLength ?? -1;

		await using var src = await response.Content.ReadAsStreamAsync();
		await using var dst = File.Create(dest);
		var buffer = new byte[81920];
		long read = 0;
		int n;
		while ((n = await src.ReadAsync(buffer)) > 0) {
			await dst.WriteAsync(buffer.AsMemory(0, n));
			read += n;
			if (total > 0) progress?.Report((double)read / total);
		}
	}

	private static void ApplyLinux(string tarball, string dir) {
		var root = BuildInfo.InstallRoot.TrimEnd('/');
		var staged = root + ".new";
		var old = root + ".old";
		if (Directory.Exists(staged)) Directory.Delete(staged, true);
		Directory.CreateDirectory(staged);

		using (var tar = Process.Start("tar", $"-xzf \"{tarball}\" -C \"{staged}\" --strip-components=1")) {
			tar!.WaitForExit();
			if (tar.ExitCode != 0) throw new IOException("Failed to extract the update");
		}

		var script = Path.Combine(dir, "update.sh");
		File.WriteAllText(script, $"""
			#!/bin/sh
			while kill -0 {Environment.ProcessId} 2>/dev/null; do sleep 0.2; done
			rm -rf "{old}"
			mv "{root}" "{old}" && mv "{staged}" "{root}" && rm -rf "{old}"
			chmod +x "{root}/editor/editor" 2>/dev/null
			exec "{root}/editor/editor"
			""".Replace("\r\n", "\n") + "\n");

		Process.Start(new ProcessStartInfo("setsid", $"sh \"{script}\"") { UseShellExecute = false });
	}
}
