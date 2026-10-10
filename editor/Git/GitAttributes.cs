using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace editor.Git;

public sealed class GitAttributes {
	private sealed record Rule(string BaseDir, Regex Pattern, bool Anchored, bool? Lockable, bool? Lfs);

	private readonly List<Rule> m_rules = [];
	private readonly bool m_ignoreCase = OperatingSystem.IsWindows();

	public static GitAttributes Load(string repoRoot, string projectPath) {
		var attributes = new GitAttributes();
		var dirs = new List<string> { repoRoot };
		var project = Path.GetFullPath(projectPath);
		if (!string.Equals(project.TrimEnd(Path.DirectorySeparatorChar), repoRoot.TrimEnd(Path.DirectorySeparatorChar),
			    StringComparison.OrdinalIgnoreCase))
			dirs.Add(project);

		attributes.LoadFile(Path.Combine(repoRoot, ".git", "info", "attributes"), repoRoot);
		foreach (var dir in dirs) attributes.LoadFile(Path.Combine(dir, ".gitattributes"), dir);
		return attributes;
	}

	public static GitAttributes FromText(string text, string baseDir) {
		var attributes = new GitAttributes();
		attributes.Parse(text, baseDir);
		return attributes;
	}

	private void LoadFile(string path, string baseDir) {
		try {
			if (File.Exists(path)) Parse(File.ReadAllText(path, Encoding.UTF8), baseDir);
		} catch {
			// unreadable attributes just mean nothing is lockable
		}
	}

	private void Parse(string text, string baseDir) {
		var normalizedBase = Path.GetFullPath(baseDir).TrimEnd(Path.DirectorySeparatorChar);
		foreach (var raw in text.Split('\n')) {
			var line = raw.Trim();
			if (line.Length == 0 || line[0] == '#' || line.StartsWith("[attr]", StringComparison.Ordinal)) continue;

			var parts = line.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
			if (parts.Length < 2) continue;

			var pattern = parts[0];
			if (pattern.EndsWith('/')) continue; // directory patterns don't set attributes on the files inside

			bool? lockable = null;
			bool? lfs = null;
			for (var i = 1; i < parts.Length; i++) {
				switch (parts[i]) {
					case "lockable": lockable = true; break;
					case "-lockable": lockable = false; break;
					case "filter=lfs": lfs = true; break;
					case "-filter": lfs = false; break;
					default:
						if (parts[i].StartsWith("filter=", StringComparison.Ordinal)) lfs = false;
						break;
				}
			}

			if (lockable is null && lfs is null) continue;

			var anchored = pattern.TrimStart('/').Contains('/');
			var options = RegexOptions.Compiled | (m_ignoreCase ? RegexOptions.IgnoreCase : RegexOptions.None);
			m_rules.Add(new Rule(normalizedBase, new Regex("^" + GlobToRegex(pattern.TrimStart('/')) + "$", options),
				anchored, lockable, lfs));
		}
	}

	public bool IsLockable(string absolutePath) {
		bool? lockable = null;
		bool? lfs = null;
		var full = Path.GetFullPath(absolutePath);
		var name = Path.GetFileName(full);

		foreach (var rule in m_rules) {
			string subject;
			if (rule.Anchored) {
				if (!full.StartsWith(rule.BaseDir + Path.DirectorySeparatorChar,
					    m_ignoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal))
					continue;
				subject = full[(rule.BaseDir.Length + 1)..].Replace(Path.DirectorySeparatorChar, '/');
			} else {
				if (!full.StartsWith(rule.BaseDir + Path.DirectorySeparatorChar,
					    m_ignoreCase ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal))
					continue;
				subject = name;
			}

			if (!rule.Pattern.IsMatch(subject)) continue;
			if (rule.Lockable is not null) lockable = rule.Lockable;
			if (rule.Lfs is not null) lfs = rule.Lfs;
		}

		return lockable ?? lfs ?? false;
	}

	private static string GlobToRegex(string glob) {
		var sb = new StringBuilder();
		for (var i = 0; i < glob.Length; i++) {
			var c = glob[i];
			switch (c) {
				case '*':
					if (i + 1 < glob.Length && glob[i + 1] == '*') {
						// "**/" matches zero or more directories, a bare "**" matches anything
						if (i + 2 < glob.Length && glob[i + 2] == '/') {
							sb.Append("(?:.*/)?");
							i += 2;
						} else {
							sb.Append(".*");
							i++;
						}
					} else {
						sb.Append("[^/]*");
					}

					break;
				case '?':
					sb.Append("[^/]");
					break;
				case '[': {
					var end = glob.IndexOf(']', i + 1);
					if (end < 0) {
						sb.Append("\\[");
					} else {
						var body = glob[(i + 1)..end];
						if (body.StartsWith('!')) body = "^" + body[1..];
						sb.Append('[').Append(body.Replace("\\", "\\\\")).Append(']');
						i = end;
					}

					break;
				}
				case '\\' when i + 1 < glob.Length:
					sb.Append(Regex.Escape(glob[++i].ToString()));
					break;
				default:
					sb.Append(Regex.Escape(c.ToString()));
					break;
			}
		}

		return sb.ToString();
	}
}
