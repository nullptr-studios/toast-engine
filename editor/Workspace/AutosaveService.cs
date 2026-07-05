using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Media;
using Avalonia.Styling;
using Avalonia.Threading;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.Engine;
using Lucide.Avalonia;

namespace editor.Workspace;

public interface IAutosavable {
	bool IsAutosaveDirty { get; }
	string? AutosaveFileName { get; }
	Task WriteAutosaveAsync(string virtualPath);
}

// Writes every dirty open editor to cache://autosaves/<uid><ext> once a minute
public sealed class AutosaveService {
	private const string AutosaveDir = "cache://autosaves/";
	private static readonly TimeSpan Interval = TimeSpan.FromMinutes(1);

	private readonly Func<IEnumerable<IAutosavable>> m_sources;
	private readonly DispatcherTimer m_timer;
	private bool m_running;

	public AutosaveService(Func<IEnumerable<IAutosavable>> sources) {
		m_sources = sources;
		m_timer = new DispatcherTimer { Interval = Interval };
		m_timer.Tick += async (_, _) => await RequestAutosave();
		m_timer.Start();
	}

	public void Stop() {
		m_timer.Stop();
	}

	public async Task RequestAutosave() {
		if (m_running) return;
		m_running = true;
		try {
			Directory.CreateDirectory(ProjectContext.Resolve(AutosaveDir));
			foreach (var source in m_sources()) {
				if (!source.IsAutosaveDirty || source.AutosaveFileName is not { } name) continue;
				try {
					await source.WriteAutosaveAsync(VirtualPath(name));
				} catch (Exception e) {
					Log.Warn($"Autosave of '{name}' failed: {e.Message}");
				}
			}
		} finally {
			m_running = false;
		}
	}

	public static string VirtualPath(string fileName) {
		return AutosaveDir + fileName;
	}

	public static string RealPath(string uid, string ext) {
		return ProjectContext.Resolve(VirtualPath(uid + ext));
	}

	// drops the autosave for the given asset; called after saves and "Don't Save"
	public static void Delete(string? uid, string? ext) {
		if (string.IsNullOrEmpty(uid) || string.IsNullOrEmpty(ext)) return;
		try {
			var path = RealPath(uid, ext);
			if (File.Exists(path)) File.Delete(path);
		} catch {
			// ignored
		}
	}

	// checks for an autosave newer than the asset's .meta modified_at and prompts to recover it
	public static async Task<string?> TryRecoverAsync(string uid, string virtualPath) {
		var ext = AssetTypeRegistry.GetExtension(virtualPath);
		var autosavePath = RealPath(uid, ext);
		if (!File.Exists(autosavePath)) return null;

		var assetTime = DateTime.MinValue;
		if (MetaFile.ReadHeader(ProjectContext.Resolve(virtualPath))?.ModifiedAt is { } modified &&
		    DateTime.TryParse(modified, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out var parsed))
			assetTime = parsed.ToUniversalTime();

		// the asset was saved after the autosave
		if (File.GetLastWriteTimeUtc(autosavePath) <= assetTime) {
			Delete(uid, ext);
			return null;
		}

		if (App.MainWindow is not { } owner) return null;

		static IBrush? Brush(string key) {
			if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true)
				return r as IBrush;
			return null;
		}

		var recover = await new MessageModal(new ModalConfig(
			"Recover Autosave",
			$"'{Path.GetFileName(virtualPath)}' has autosaved changes newer than the file. Recover them?",
			ModalButtons.OkCancel,
			LucideIconKind.FileClock,
			Brush("Orange"),
			"Recover",
			CancelLabel: "Discard",
			OkIcon: LucideIconKind.ClockFading
		)).ShowDialog<bool?>(owner);

		if (recover is not true) {
			Delete(uid, ext);
			return null;
		}

		return autosavePath;
	}
}
