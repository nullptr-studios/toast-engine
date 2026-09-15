//
// LayoutStore.cs by Xein
// 1 Aug 2026
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using editor.Assets;

namespace editor.Workspace;

public static class LayoutStore {
	public const string DefaultName = "Default";
	private const string SessionFileName = "_session.json";

	private static readonly JsonSerializerOptions s_options = new() {
		PropertyNameCaseInsensitive = true,
		WriteIndented = true,
		DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
	};

	private static string Dir => ProjectContext.Resolve("cache://layouts");

	private static string PathFor(string name) {
		return Path.Combine(Dir, name + ".json");
	}

	public static bool IsBuiltin(string? name) {
		return string.IsNullOrEmpty(name) || string.Equals(name, DefaultName, StringComparison.OrdinalIgnoreCase);
	}

	public static bool IsValidName(string? name) {
		return !string.IsNullOrWhiteSpace(name)
			&& !IsBuiltin(name)
			&& name.IndexOfAny(Path.GetInvalidFileNameChars()) < 0
			&& !string.Equals(name, Path.GetFileNameWithoutExtension(SessionFileName),
				StringComparison.OrdinalIgnoreCase);
	}

	public static IReadOnlyList<string> EnumerateNames() {
		if (!ProjectContext.IsInitialized) return [];
		try {
			if (!Directory.Exists(Dir)) return [];
			return Directory.EnumerateFiles(Dir, "*.json")
				.Where(f => !string.Equals(Path.GetFileName(f), SessionFileName, StringComparison.OrdinalIgnoreCase))
				.Select(Path.GetFileNameWithoutExtension)
				.Where(n => !string.IsNullOrEmpty(n))
				.Select(n => n!)
				.OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
				.ToList();
		} catch {
			return [];
		}
	}

	public static bool Exists(string name) {
		if (!ProjectContext.IsInitialized || !IsValidName(name)) return false;
		try {
			return File.Exists(PathFor(name));
		} catch {
			return false;
		}
	}

	public static LayoutFile? Load(string name) {
		if (!IsValidName(name)) return null;
		return ReadFile(PathFor(name));
	}

	public static void Save(string name, LayoutFile file) {
		if (!IsValidName(name)) return;
		WriteFile(PathFor(name), file);
	}

	public static void Delete(string name) {
		if (!ProjectContext.IsInitialized || !IsValidName(name)) return;
		try {
			var path = PathFor(name);
			if (File.Exists(path)) File.Delete(path);
		} catch {
			// best-effort
		}
	}

	public static LayoutFile? LoadSession() {
		return ReadFile(ProjectContext.IsInitialized ? Path.Combine(Dir, SessionFileName) : "");
	}

	public static void SaveSession(LayoutFile file) {
		if (!ProjectContext.IsInitialized) return;
		WriteFile(Path.Combine(Dir, SessionFileName), file);
	}

	private static LayoutFile? ReadFile(string path) {
		if (!ProjectContext.IsInitialized || string.IsNullOrEmpty(path)) return null;
		try {
			if (!File.Exists(path)) return null;
			var file = JsonSerializer.Deserialize<LayoutFile>(File.ReadAllText(path), s_options);
			if (file is null || file.Version > 1) return null;
			return file;
		} catch {
			return null; // fall back to default if corrupt
		}
	}

	private static void WriteFile(string path, LayoutFile file) {
		try {
			Directory.CreateDirectory(Dir);
			var temp = path + ".tmp";
			File.WriteAllText(temp, JsonSerializer.Serialize(file, s_options));
			File.Move(temp, path, true);
		} catch {
			// best-effort
		}
	}
}
