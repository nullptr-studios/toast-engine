//
// UIBindStubGenerator.cs by Xein
// 18 Jul 2026
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace editor.Assets;

public static class UIBindStubGenerator {
	private static readonly HashSet<string> ExpressionKeywords =
		["true", "false", "and", "or", "not", "it", "it_index", "ev"];

	// Writes {project}/.toast/lua/ui_binds.d.lua covering every .rml under the content and core roots
	public static void Generate() {
		if (!ProjectContext.IsInitialized) return;

		var classes = new List<string>();
		var seenStems = new HashSet<string>(StringComparer.Ordinal);

		foreach (var file in EnumerateRmlFiles()) {
			string text;
			try {
				text = File.ReadAllText(file);
			} catch {
				continue;
			}

			var (binds, boolBinds, events) = Scan(text);
			if (binds.Count == 0 && events.Count == 0) continue;

			// Keep class names unique and Lua-identifier safe
			var stem = SanitizeStem(Path.GetFileNameWithoutExtension(file));
			var name = stem;
			var n = 1;
			while (!seenStems.Add(name)) name = $"{stem}_{n++}";

			classes.Add(EmitClass(name, binds, boolBinds, events));
		}

		var sb = new StringBuilder();
		sb.AppendLine("---@meta");
		sb.AppendLine("-- Generated from project .rml documents — do not edit by hand.");
		sb.AppendLine("-- Annotate a panel handle to get completion, e.g. `---@type UIBinds.hud`.");
		sb.AppendLine();
		foreach (var c in classes) {
			sb.Append(c);
			sb.AppendLine();
		}

		sb.AppendLine("---@class Panel");
		sb.AppendLine("---@field ui_binds any");
		sb.AppendLine();
		sb.AppendLine("---@class Panel3D");
		sb.AppendLine("---@field ui_binds any");

		var dstDir = Path.Combine(ProjectContext.CachePath, "lua");
		Directory.CreateDirectory(dstDir);
		File.WriteAllText(Path.Combine(dstDir, "ui_binds.d.lua"), sb.ToString());
	}

	private static IEnumerable<string> EnumerateRmlFiles() {
		var roots = ProjectContext.DatabaseRoots.Append(ProjectContext.CorePath);
		foreach (var root in roots) {
			if (!Directory.Exists(root)) continue;
			foreach (var file in Directory.EnumerateFiles(root, "*.rml", SearchOption.AllDirectories))
				yield return file;
		}
	}

	private static string EmitClass(string name, List<string> binds, HashSet<string> boolBinds, List<string> events) {
		var sb = new StringBuilder();
		if (events.Count > 0)
			sb.AppendLine($"-- events: {string.Join(", ", events.Select(e => e + "()"))}");
		sb.AppendLine($"---@class UIBinds.{name}");
		foreach (var bind in binds) {
			var type = boolBinds.Contains(bind) ? "boolean" : "any";
			sb.AppendLine($"---@field {bind} {type}");
		}

		return sb.ToString();
	}

	private static string SanitizeStem(string stem) {
		var sb = new StringBuilder();
		foreach (var c in stem)
			sb.Append(char.IsLetterOrDigit(c) || c == '_' ? c : '_');
		var result = sb.ToString();
		if (result.Length == 0 || char.IsDigit(result[0])) result = "_" + result;
		return result;
	}

	private static (List<string> binds, HashSet<string> boolBinds, List<string> events) Scan(string rml) {
		var binds = new List<string>();
		var boolBinds = new HashSet<string>(StringComparer.Ordinal);
		var events = new List<string>();

		var i = 0;
		while (i < rml.Length) {
			if (Match(rml, i, "<!--")) {
				var end = rml.IndexOf("-->", i, StringComparison.Ordinal);
				i = end < 0 ? rml.Length : end + 3;
				continue;
			}

			if (Match(rml, i, "{{")) {
				var end = rml.IndexOf("}}", i, StringComparison.Ordinal);
				if (end < 0) break;
				CollectIdentifiers(rml.Substring(i + 2, end - i - 2), binds);
				i = end + 2;
				continue;
			}

			if (rml[i] == '<') {
				var tagEnd = rml.IndexOf('>', i);
				var tag = rml.Substring(i, (tagEnd < 0 ? rml.Length : tagEnd) - i);
				ScanTag(tag, binds, boolBinds, events);
				i = tagEnd < 0 ? rml.Length : tagEnd + 1;
				continue;
			}

			i++;
		}

		return (binds, boolBinds, events);
	}

	private static void ScanTag(string tag, List<string> binds, HashSet<string> boolBinds, List<string> events) {
		var a = 0;
		while (a < tag.Length) {
			while (a < tag.Length && !char.IsWhiteSpace(tag[a])) a++;
			while (a < tag.Length && char.IsWhiteSpace(tag[a])) a++;
			var nameStart = a;
			while (a < tag.Length && tag[a] != '=' && !char.IsWhiteSpace(tag[a])) a++;
			var attr = tag.Substring(nameStart, a - nameStart);
			if (a >= tag.Length || tag[a] != '=') continue;
			a++;
			if (a >= tag.Length || tag[a] != '"') continue;
			a++;
			var valueStart = a;
			while (a < tag.Length && tag[a] != '"') a++;
			var value = tag.Substring(valueStart, a - valueStart);
			a++;

			if (attr.StartsWith("data-event-", StringComparison.Ordinal)) {
				CollectEventName(value, events);
			} else if (attr.StartsWith("on", StringComparison.Ordinal) && value.Contains('(')) {
				CollectEventName(value, events);
			} else if (attr == "data-for") {
				var colon = value.IndexOf(':');
				if (colon >= 0) CollectIdentifiers(value[(colon + 1)..], binds);
			} else if (attr.StartsWith("data-", StringComparison.Ordinal) && attr != "data-model" &&
			           !attr.StartsWith("data-loc-", StringComparison.Ordinal)) {
				CollectIdentifiers(value, binds);
				// data-checked round-trips a boolean, so mark its identifiers for the stub type
				if (attr == "data-checked")
					foreach (var name in ExtractIdentifiers(value))
						boolBinds.Add(name);
			}
		}
	}

	private static void CollectIdentifiers(string expression, List<string> outList) {
		foreach (var id in ExtractIdentifiers(expression))
			if (!outList.Contains(id))
				outList.Add(id);
	}

	private static IEnumerable<string> ExtractIdentifiers(string expression) {
		var i = 0;
		while (i < expression.Length) {
			var c = expression[i];
			if (c == '\'') {
				i++;
				while (i < expression.Length && expression[i] != '\'') i++;
				i++;
				continue;
			}

			if (char.IsLetter(c) || c == '_') {
				var start = i;
				while (i < expression.Length && (char.IsLetterOrDigit(expression[i]) || expression[i] == '_')) i++;
				var word = expression.Substring(start, i - start);
				if (i < expression.Length && expression[i] == '(') continue; // function call
				if (!ExpressionKeywords.Contains(word) && !char.IsDigit(word[0]))
					yield return word;
				while (i < expression.Length && (char.IsLetterOrDigit(expression[i]) || expression[i] == '_' || expression[i] == '.')) i++;
				continue;
			}

			i++;
		}
	}

	private static void CollectEventName(string value, List<string> events) {
		var start = 0;
		while (start < value.Length && char.IsWhiteSpace(value[start])) start++;
		var end = start;
		while (end < value.Length && (char.IsLetterOrDigit(value[end]) || value[end] == '_')) end++;
		if (end > start) {
			var name = value.Substring(start, end - start);
			if (!events.Contains(name)) events.Add(name);
		}
	}

	private static bool Match(string s, int i, string token) {
		return i + token.Length <= s.Length && string.CompareOrdinal(s, i, token, 0, token.Length) == 0;
	}
}
