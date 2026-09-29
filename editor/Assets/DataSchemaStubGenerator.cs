//
// DataSchemaStubGenerator.cs
// by Xein
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using editor.Engine;

namespace editor.Assets;

public static class DataSchemaStubGenerator {
	private const int DebounceMilliseconds = 250;

	private static readonly List<FileSystemWatcher> Watchers = [];
	private static readonly object WatchLock = new();
	private static Timer? s_debounceTimer;

	public static void StartWatching() {
		lock (WatchLock) {
			foreach (var watcher in Watchers) watcher.Dispose();
			Watchers.Clear();

			foreach (var root in ProjectContext.DatabaseRoots.Append(ProjectContext.CorePath)
				         .Distinct(StringComparer.OrdinalIgnoreCase)) {
				if (!Directory.Exists(root)) continue;
				var watcher = new FileSystemWatcher(root, "*.schema.json") {
					IncludeSubdirectories = true,
					NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite | NotifyFilters.CreationTime,
					EnableRaisingEvents = true
				};
				watcher.Changed += OnSchemaFileChanged;
				watcher.Created += OnSchemaFileChanged;
				watcher.Deleted += OnSchemaFileChanged;
				watcher.Renamed += OnSchemaFileChanged;
				Watchers.Add(watcher);
			}
		}
	}

	public static void StopWatching() {
		lock (WatchLock) {
			s_debounceTimer?.Dispose();
			s_debounceTimer = null;
			foreach (var watcher in Watchers) watcher.Dispose();
			Watchers.Clear();
		}
	}

	private static void OnSchemaFileChanged(object sender, FileSystemEventArgs e) {
		lock (WatchLock) {
			s_debounceTimer?.Dispose();
			s_debounceTimer = new Timer(_ => {
				try {
					Generate();
				} catch {
					/* just give up bro */
				}
			}, null, DebounceMilliseconds, Timeout.Infinite);
		}
	}

	public static void Generate() {
		if (!ProjectContext.IsInitialized || !ToastEngine.IsEngineReady) return;

		var dstDir = Path.Combine(ProjectContext.CachePath, "lua");
		Directory.CreateDirectory(dstDir);
		var dst = Path.Combine(dstDir, "data_schemas.d.lua");
		ToastEngine.GenerateDataSchemaStubs(dst);
	}
}
