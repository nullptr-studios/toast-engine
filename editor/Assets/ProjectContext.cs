using System;
using System.Collections.Generic;
using System.IO;
using Ktx2Sharp;

namespace editor.Assets;

/// <summary>
///    All project paths and virtual scheme resolution (assets://, cache://, etc) — needs <see cref="Initialize" />
///    first
/// </summary>
public static class ProjectContext {
	private static readonly Dictionary<string, string> s_schemes = new();

	public static string ProjectPath { get; private set; } = "";
	public static string ArtworkPath { get; private set; } = "";
	public static string AssetsPath { get; private set; } = "";
	public static string CachePath { get; private set; } = "";
	public static string CorePath { get; private set; } = "";
	public static string SavedPath { get; private set; } = "";
	public static bool IsInitialized { get; private set; }

	// Fired after an import batch completes
	public static event Action? AssetsChanged;

	public static void RaiseAssetsChanged() {
		AssetsChanged?.Invoke();
	}

	public static void Initialize(string projectPath, string corePath) {
		ProjectPath = Path.GetFullPath(projectPath);
		ArtworkPath = Path.Combine(ProjectPath, "artwork");
		AssetsPath = Path.Combine(ProjectPath, "assets");
		CachePath = Path.Combine(ProjectPath, ".toast");
		SavedPath = Path.Combine(ProjectPath, ".toast", "save_data");
		CorePath = Path.GetFullPath(corePath);

		s_schemes.Clear();
		s_schemes["assets://"] = AssetsPath;
		s_schemes["artwork://"] = ArtworkPath;
		s_schemes["cache://"] = CachePath;
		s_schemes["core://"] = CorePath;
		s_schemes["saved://"] = SavedPath;

		EnsureDirectories();
		Ktx.Init();
		IsInitialized = true;
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

	// Absolute real path → "assets://textures/foo.ktx2", null if not under any known root
	public static string? ToVirtual(string realPath) {
		var canonical = Path.GetFullPath(realPath);
		foreach (var (scheme, root) in s_schemes) {
			var rootFull = Path.GetFullPath(root) + Path.DirectorySeparatorChar;
			if (canonical.StartsWith(rootFull, StringComparison.OrdinalIgnoreCase)) {
				var relative = canonical[rootFull.Length..].Replace(Path.DirectorySeparatorChar, '/');
				return scheme + relative;
			}
		}

		return null;
	}

	private static void EnsureDirectories() {
		Directory.CreateDirectory(AssetsPath);
		Directory.CreateDirectory(ArtworkPath);
		Directory.CreateDirectory(CachePath);
		Directory.CreateDirectory(Path.Combine(CachePath, "thumbnails"));
	}
}
