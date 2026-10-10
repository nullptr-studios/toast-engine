using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace editor.Git;

public sealed record GitStatusSnapshot(GitBranchInfo Branch, IReadOnlyList<GitChange> Changes);

public static class GitParsers {
	public static GitStatusSnapshot ParseStatus(string output, string repoRoot) {
		var changes = new List<GitChange>();
		var head = "";
		var ahead = 0;
		var behind = 0;
		var hasUpstream = false;

		var tokens = output.Split('\0');
		for (var i = 0; i < tokens.Length; i++) {
			var token = tokens[i];
			if (token.Length < 2) continue;

			switch (token[0]) {
				case '#':
					ParseHeader(token, ref head, ref ahead, ref behind, ref hasUpstream);
					break;
				case '1': {
					// 1 XY sub mH mI mW hH hI path
					var parts = token.Split(' ', 9);
					if (parts.Length < 9) break;
					changes.Add(new GitChange(Abs(repoRoot, parts[8]), MapCode(parts[1][0]), MapCode(parts[1][1])));
					break;
				}
				case '2': {
					// 2 XY sub mH mI mW hH hI Xscore path \0 origPath
					var parts = token.Split(' ', 10);
					var orig = i + 1 < tokens.Length ? tokens[++i] : null;
					if (parts.Length < 10) break;
					changes.Add(new GitChange(Abs(repoRoot, parts[9]), MapCode(parts[1][0]), MapCode(parts[1][1]),
						orig is null ? null : Abs(repoRoot, orig)));
					break;
				}
				case 'u': {
					// u XY sub m1 m2 m3 mW h1 h2 h3 path
					var parts = token.Split(' ', 11);
					if (parts.Length < 11) break;
					changes.Add(new GitChange(Abs(repoRoot, parts[10]), GitFileStatus.None, GitFileStatus.Modified));
					break;
				}
				case '?':
					changes.Add(new GitChange(Abs(repoRoot, token[2..]), GitFileStatus.None, GitFileStatus.Untracked));
					break;
			}
		}

		return new GitStatusSnapshot(new GitBranchInfo(head, ahead, behind, hasUpstream), changes);
	}

	private static void ParseHeader(string token, ref string head, ref int ahead, ref int behind,
		ref bool hasUpstream) {
		if (token.StartsWith("# branch.head ", StringComparison.Ordinal)) {
			head = token["# branch.head ".Length..];
			if (head == "(detached)") head = "HEAD (detached)";
		} else if (token.StartsWith("# branch.upstream ", StringComparison.Ordinal)) {
			hasUpstream = true;
		} else if (token.StartsWith("# branch.ab ", StringComparison.Ordinal)) {
			// # branch.ab +1 -2
			var parts = token.Split(' ');
			if (parts.Length >= 4) {
				int.TryParse(parts[2].TrimStart('+'), out ahead);
				int.TryParse(parts[3].TrimStart('-'), out behind);
			}
		}
	}

	private static GitFileStatus MapCode(char c) {
		return c switch {
			'A' => GitFileStatus.Added,
			'M' or 'T' or 'U' => GitFileStatus.Modified,
			'D' => GitFileStatus.Deleted,
			'R' or 'C' => GitFileStatus.Moved,
			_ => GitFileStatus.None
		};
	}

	public static string Abs(string repoRoot, string relative) {
		return Path.GetFullPath(Path.Combine(repoRoot, relative.Replace('/', Path.DirectorySeparatorChar)));
	}

	public static string Rel(string repoRoot, string absolute) {
		return Path.GetRelativePath(repoRoot, absolute).Replace(Path.DirectorySeparatorChar, '/');
	}

	public static List<GitLock> ParseLocks(string json, string repoRoot, string? currentUser) {
		var result = new List<GitLock>();
		if (string.IsNullOrWhiteSpace(json)) return result;

		using var doc = JsonDocument.Parse(json);
		var root = doc.RootElement;
		if (root.ValueKind == JsonValueKind.Array) {
			foreach (var item in root.EnumerateArray()) {
				var owner = OwnerName(item);
				AddLock(result, item, repoRoot, owner, currentUser is not null && owner == currentUser);
			}
		} else if (root.ValueKind == JsonValueKind.Object) {
			if (root.TryGetProperty("ours", out var ours) && ours.ValueKind == JsonValueKind.Array)
				foreach (var item in ours.EnumerateArray())
					AddLock(result, item, repoRoot, OwnerName(item), true);
			if (root.TryGetProperty("theirs", out var theirs) && theirs.ValueKind == JsonValueKind.Array)
				foreach (var item in theirs.EnumerateArray())
					AddLock(result, item, repoRoot, OwnerName(item), false);
		}

		return result;
	}

	private static string OwnerName(JsonElement item) {
		return item.TryGetProperty("owner", out var owner) && owner.TryGetProperty("name", out var name)
			? name.GetString() ?? ""
			: "";
	}

	private static void AddLock(List<GitLock> list, JsonElement item, string repoRoot, string owner, bool mine) {
		if (!item.TryGetProperty("path", out var path) || path.GetString() is not { Length: > 0 } rel) return;
		var id = item.TryGetProperty("id", out var idElement) ? idElement.ToString() : "";
		var lockedAt = DateTime.MinValue;
		if (item.TryGetProperty("locked_at", out var at) && at.GetString() is { } text)
			DateTime.TryParse(text, null, System.Globalization.DateTimeStyles.AdjustToUniversal, out lockedAt);
		list.Add(new GitLock(id, Abs(repoRoot, rel), owner, mine, lockedAt));
	}

	public static Dictionary<string, string> ParseAuthors(string output) {
		var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries)) {
			var parts = line.TrimEnd('\r').Split('\t');
			if (parts.Length < 2 || parts[0].Length == 0 || parts[1].Length == 0) continue;
			map.TryAdd(parts[0], parts[1]);
			map.TryAdd(parts[1], parts[1]);
		}

		return map;
	}

	public static string? FindEmail(IReadOnlyDictionary<string, string> authors, string owner) {
		if (owner.Length == 0) return null;
		if (authors.TryGetValue(owner, out var byName)) return byName;

		foreach (var email in authors.Values) {
			var local = email.Split('@')[0];
			var plus = local.IndexOf('+');
			if (plus >= 0) local = local[(plus + 1)..];
			if (string.Equals(local, owner, StringComparison.OrdinalIgnoreCase)) return email;
		}

		return null;
	}

	private static readonly Regex s_grepUid = new(@"^HEAD:(?<path>.+?):\d+:uid\s*=\s*""?(?<uid>[^""\s]+)""?\s*$",
		RegexOptions.Compiled);

	public static Dictionary<string, string> ParseGrepUids(string output) {
		var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries)) {
			var match = s_grepUid.Match(line.TrimEnd('\r'));
			if (match.Success) map[match.Groups["path"].Value] = match.Groups["uid"].Value;
		}

		return map;
	}

	public static List<GitChange> PairMoves(IReadOnlyList<GitChange> changes, IReadOnlyDictionary<string, string> oldUids,
		Func<string, string?> readNewUid) {
		var byOldUid = new Dictionary<string, string>();
		foreach (var kv in oldUids) byOldUid[kv.Value] = kv.Key;

		var result = new List<GitChange>(changes);
		var index = result.Select((c, i) => (c.Path, i)).ToDictionary(t => t.Path, t => t.i);
		var movedMetas = new HashSet<string>();

		for (var i = 0; i < result.Count; i++) {
			var change = result[i];
			if (!change.Path.EndsWith(".meta", StringComparison.OrdinalIgnoreCase)) continue;
			var isNew = change.Unstaged == GitFileStatus.Untracked || (change.Staged & GitFileStatus.Added) != 0;
			if (!isNew) continue;
			var uid = readNewUid(change.Path);
			if (uid is null || !byOldUid.ContainsKey(uid)) continue;

			result[i] = Promote(change);
			movedMetas.Add(change.Path);
		}

		// The asset next to a moved .meta moved with it
		foreach (var meta in movedMetas) {
			var asset = meta[..^".meta".Length];
			if (!index.TryGetValue(asset, out var i)) continue;
			var change = result[i];
			if (change.Unstaged == GitFileStatus.Untracked || (change.Staged & GitFileStatus.Added) != 0)
				result[i] = Promote(change);
		}

		return result;
	}

	private static GitChange Promote(GitChange change) {
		var staged = (change.Staged & GitFileStatus.Added) != 0 ? GitFileStatus.Moved : change.Staged;
		var unstaged = change.Unstaged == GitFileStatus.Untracked ? GitFileStatus.Moved : change.Unstaged;
		return change with { Staged = staged, Unstaged = unstaged };
	}
}
