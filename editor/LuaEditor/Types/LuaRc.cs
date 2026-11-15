using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace editor.LuaEditor.Types;

public sealed record LuaRc(string RuntimeVersion, EquatableArray<string> Libraries) {
	public const string SupportedVersion = "Lua 5.4";

	private static readonly JsonDocumentOptions s_options = new() {
		CommentHandling = JsonCommentHandling.Skip,
		AllowTrailingCommas = true,
	};

	public static LuaRc Load(string projectRoot, List<Diagnostic> diagnostics) {
		var path = Path.Combine(projectRoot, ".luarc.json");
		var at = new SourceLocation(path, 1);
		if (!File.Exists(path)) {
			diagnostics.Add(new Diagnostic(DiagnosticSeverity.Info, "no .luarc.json; only the standard library is loaded", at));
			return new LuaRc(SupportedVersion, []);
		}

		JsonDocument doc;
		try {
			doc = JsonDocument.Parse(File.ReadAllText(path), s_options);
		} catch (JsonException e) {
			diagnostics.Add(new Diagnostic(DiagnosticSeverity.Error, $".luarc.json is not valid JSON: {e.Message}", at));
			return new LuaRc(SupportedVersion, []);
		} catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
			diagnostics.Add(new Diagnostic(DiagnosticSeverity.Error, $"cannot read .luarc.json: {e.Message}", at));
			return new LuaRc(SupportedVersion, []);
		}

		using (doc) {
			var root = doc.RootElement;
			var version = Setting(root, "runtime", "version") is { ValueKind: JsonValueKind.String } v ? v.GetString()! : SupportedVersion;
			if (version != SupportedVersion)
				diagnostics.Add(new Diagnostic(DiagnosticSeverity.Warning, $"runtime.version is \"{version}\"; the graph editor checks against {SupportedVersion}", at));

			var libraries = new List<string>();
			if (Setting(root, "workspace", "library") is { ValueKind: JsonValueKind.Array } list) {
				foreach (var entry in list.EnumerateArray()) {
					if (entry.ValueKind != JsonValueKind.String) continue;
					var full = Path.GetFullPath(Path.Combine(projectRoot, entry.GetString()!));
					libraries.Add(full);
					if (!Directory.Exists(full) && !File.Exists(full))
						diagnostics.Add(new Diagnostic(DiagnosticSeverity.Warning, $"workspace.library entry \"{entry.GetString()}\" does not exist", at));
				}
			}
			return new LuaRc(version, libraries);
		}
	}

	private static JsonElement? Setting(JsonElement root, string section, string key) {
		if (root.ValueKind != JsonValueKind.Object) return null;
		if (root.TryGetProperty($"{section}.{key}", out var dotted)) return dotted;
		if (root.TryGetProperty(section, out var nested) && nested.ValueKind == JsonValueKind.Object && nested.TryGetProperty(key, out var value))
			return value;
		return null;
	}
}
