using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using editor.Engine;
using Proto.Events;
using Tomlyn;
using Tomlyn.Model;

namespace editor.Assets;

public static class ProjectContext {
	private static readonly Dictionary<string, string> s_schemes = new();

	public static string ProjectPath { get; private set; } = "";
	public static string ArtworkPath { get; private set; } = "";
	public static string AssetsPath { get; private set; } = "";
	public static string CachePath { get; private set; } = "";
	public static string CorePath { get; private set; } = "";
	public static string SavedPath { get; private set; } = "";
	public static bool IsInitialized { get; private set; }

	public static IReadOnlyList<string> Databases { get; private set; } = ["assets"];

	public static IReadOnlyList<string> Languages { get; private set; } = ["en"];

	public static IEnumerable<string> DatabaseRoots => Databases.Select(db => Path.Combine(ProjectPath, db));

	// Fired after an import batch completes
	public static event Action? AssetsChanged;

	public static void RaiseAssetsChanged() {
		AssetsChanged?.Invoke();
		Events.Send(new ReloadAssetsManifest());
	}

	public static void Initialize(string projectPath, string corePath) {
		ProjectPath = Path.GetFullPath(projectPath);
		ArtworkPath = Path.Combine(ProjectPath, "artwork");
		AssetsPath = Path.Combine(ProjectPath, "assets");
		CachePath = Path.Combine(ProjectPath, ".toast");
		SavedPath = Path.Combine(ProjectPath, ".toast", "save_data");
		CorePath = Path.GetFullPath(corePath);

		// Read databases from the project .toast file (default to ["assets"])
		Databases = ReadDatabasesFromProject(ProjectPath);
		Languages = ReadLanguagesFromProject(ProjectPath);

		RegisterSchemes();
		EnsureDirectories();
		IsInitialized = true;
	}

	public static void SyncLuaDefinitions(Action<string>? log = null) {
		var src = Path.Combine(CorePath, "lua");
		var dst = Path.Combine(CachePath, "lua");
		Directory.CreateDirectory(dst);

		if (Directory.Exists(src)) {
			var count = 0;
			foreach (var file in Directory.EnumerateFiles(src, "*.lua")) {
				File.Copy(file, Path.Combine(dst, Path.GetFileName(file)), true);
				count++;
			}

			log?.Invoke($"Copied {count} lua definition file(s) -> {dst}");
		} else {
			log?.Invoke($"warning: engine lua stubs not found at {src}");
		}

		// Emit UI bind stubs alongside the engine definitions
		UIBindStubGenerator.Generate();

		var luarc = Path.Combine(ProjectPath, ".luarc.json");
		if (File.Exists(luarc)) return;
		File.WriteAllText(luarc,
			"""
			{
				"$schema": "https://raw.githubusercontent.com/LuaLS/lua-language-server/master/setting/schema.json",
				"runtime.version": "Lua 5.4",
				"workspace.library": [".toast/lua"],
				"workspace.checkThirdParty": false
			}
			""" + Environment.NewLine);
		log?.Invoke("Wrote .luarc.json");
	}

	public static void ReloadProjectSettings() {
		if (!IsInitialized) return;

		Databases = ReadDatabasesFromProject(ProjectPath);
		Languages = ReadLanguagesFromProject(ProjectPath);
		RegisterSchemes();
		EnsureDirectories();

		ToastEngine.ReloadProjectSettings();
		AssetDatabase.RebuildAssetDatabase();
		RaiseAssetsChanged();
	}

	// "assets://textures/foo.ktx2" → real absolute path
	public static string Resolve(string virtualPath) {
		foreach (var (scheme, root) in s_schemes)
			if (virtualPath.StartsWith(scheme, StringComparison.OrdinalIgnoreCase)) {
				var relative = virtualPath[scheme.Length..].Replace('/', Path.DirectorySeparatorChar);
				return Path.GetFullPath(Path.Combine(root, relative));
			}

		return Path.GetFullPath(virtualPath);
	}

	// Absolute real path "assets://textures/foo.ktx2", null if not under any known root
	// Uses longest-root-match so that assets:// wins over project://
	public static string? ToVirtual(string realPath) {
		var canonical = Path.GetFullPath(realPath);
		string? bestScheme = null;
		string? bestRelative = null;
		var bestLen = -1;

		foreach (var (scheme, root) in s_schemes) {
			var rootFull = Path.GetFullPath(root) + Path.DirectorySeparatorChar;
			if (canonical.StartsWith(rootFull, StringComparison.OrdinalIgnoreCase) && rootFull.Length > bestLen) {
				bestLen = rootFull.Length;
				bestScheme = scheme;
				bestRelative = canonical[rootFull.Length..].Replace(Path.DirectorySeparatorChar, '/');
			}
		}

		return bestScheme is not null ? bestScheme + bestRelative : null;
	}

	public static bool IsUnderContentDatabase(string realPath) {
		var canonical = Path.GetFullPath(realPath);
		foreach (var root in DatabaseRoots) {
			var rootFull = Path.GetFullPath(root) + Path.DirectorySeparatorChar;
			if (canonical.StartsWith(rootFull, StringComparison.OrdinalIgnoreCase))
				return true;
		}

		return false;
	}

	public static bool IsDatabaseRoot(string realPath) {
		var canonical = Path.GetFullPath(realPath);
		foreach (var root in DatabaseRoots)
			if (string.Equals(canonical, Path.GetFullPath(root), StringComparison.OrdinalIgnoreCase))
				return true;
		return false;
	}

	private static void RegisterSchemes() {
		s_schemes.Clear();

		// Fixed special schemes
		s_schemes["project://"] = ProjectPath;
		s_schemes["artwork://"] = ArtworkPath;
		s_schemes["cache://"] = CachePath;
		s_schemes["core://"] = CorePath;
		s_schemes["saved://"] = SavedPath;

		// Dynamic content database schemes derived from the project's databases list
		foreach (var db in Databases)
			s_schemes[db + "://"] = Path.Combine(ProjectPath, db);
	}

	private static IReadOnlyList<string> ReadDatabasesFromProject(string projectPath) {
		try {
			var toastFile = Directory.EnumerateFiles(projectPath, "*.toast").FirstOrDefault();
			if (toastFile is null) return ["assets"];

			var table = TomlSerializer.Deserialize<TomlTable>(File.ReadAllText(toastFile));
			if (table?["databases"] is TomlArray dbs) {
				var list = dbs.Select(d => d?.ToString() ?? "").Where(s => s.Length > 0).ToList();
				return list.Count > 0 ? list : ["assets"];
			}
		} catch {
			// ignored
		}

		return ["assets"];
	}

	private static IReadOnlyList<string> ReadLanguagesFromProject(string projectPath) {
		try {
			var toastFile = Directory.EnumerateFiles(projectPath, "*.toast").FirstOrDefault();
			if (toastFile is null) return ["en"];

			var table = TomlSerializer.Deserialize<TomlTable>(File.ReadAllText(toastFile));
			if (table?["ui"] is TomlTable ui && ui["languages"] is TomlArray langs) {
				var list = langs.Select(l => l?.ToString() ?? "").Where(s => s.Length > 0).ToList();
				return list.Count > 0 ? list : ["en"];
			}
		} catch {
			// ignored
		}

		return ["en"];
	}

	private static void EnsureDirectories() {
		Directory.CreateDirectory(AssetsPath);
		Directory.CreateDirectory(ArtworkPath);
		Directory.CreateDirectory(CachePath);
		Directory.CreateDirectory(Path.Combine(CachePath, "thumbnails"));
		Directory.CreateDirectory(Path.Combine(CachePath, "autosaves"));

		foreach (var root in DatabaseRoots)
			Directory.CreateDirectory(root);
	}
}
