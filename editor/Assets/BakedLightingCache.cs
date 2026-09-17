using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace editor.Assets;

/// <summary>
/// Finds baked lighting files under <c>cache://</c> whose node no longer exists anywhere in the project
/// </summary>
/// <remarks>
/// A reflection probe or irradiance volume writes its bake keyed by the node's uid, and nothing deletes it
/// when that node goes away - a bake is expensive enough that discarding it on a guess is worse than leaving
/// it. Node lifetime is the wrong signal to clean up on: a node unregisters whenever a scene closes or the
/// world is torn down, which says nothing at all about whether the probe still exists in the project
///
/// So the question is answered against the files on disk instead: a bake is orphaned when its uid appears in
/// no scene or prefab anywhere under the project's databases. That is deliberately conservative - a uid found
/// for any reason at all keeps its bake - because the cost of a false positive (minutes of re-baking, or an
/// unopened level silently losing its lighting) is far higher than the cost of a stale file surviving
/// </remarks>
public static class BakedLightingCache {
	/// <param name="Kind">"Reflection probe" or "Irradiance volume", for the confirmation prompt</param>
	public record OrphanedBake(string Path, string Uid, string Kind, long Bytes);

	private static readonly string[] s_sceneExtensions = [".tnode", ".node"];

	/// <summary>Baked files with no referencing node, newest scan each call.</summary>
	public static List<OrphanedBake> FindOrphans() {
		var orphans = new List<OrphanedBake>();
		if (!ProjectContext.IsInitialized) return orphans;

		var candidates = new List<(string Path, string Uid, string Kind)>();
		CollectBakes(System.IO.Path.Combine(ProjectContext.CachePath, "probes"), "*.tprobe", "Reflection probe", candidates);
		CollectBakes(System.IO.Path.Combine(ProjectContext.CachePath, "irradiance"), "*.tsh", "Irradiance volume", candidates);
		if (candidates.Count == 0) return orphans;

		// Every uid still referenced, gathered once. Scenes are binary, so a uid is stored as the raw 64-bit
		// value rather than the base64url text the filename uses - both forms are searched, since a text
		// .node file carries the string instead
		var referenced = CollectReferencedUids(candidates);

		foreach (var (path, uid, kind) in candidates) {
			if (referenced.Contains(uid)) continue;
			var size = new FileInfo(path).Length;
			orphans.Add(new OrphanedBake(path, uid, kind, size));
		}

		return orphans;
	}

	/// <summary>Deletes <paramref name="orphans" />, skipping any that fail rather than aborting the batch.</summary>
	/// <returns>How many were removed and how many bytes that freed.</returns>
	public static (int Deleted, long Bytes, List<string> Failed) Delete(IEnumerable<OrphanedBake> orphans) {
		var deleted = 0;
		long bytes = 0;
		var failed = new List<string>();

		foreach (var orphan in orphans)
			try {
				File.Delete(orphan.Path);
				deleted++;
				bytes += orphan.Bytes;
			}
			catch (Exception) {
				// One locked or read-only file must not stop the rest; the caller reports what survived
				failed.Add(System.IO.Path.GetFileName(orphan.Path));
			}

		return (deleted, bytes, failed);
	}

	public static string FormatSize(long bytes) {
		return bytes switch {
			>= 1024L * 1024 * 1024 => $"{bytes / (1024.0 * 1024 * 1024):0.##} GB",
			>= 1024 * 1024 => $"{bytes / (1024.0 * 1024):0.##} MB",
			>= 1024 => $"{bytes / 1024.0:0.##} KB",
			_ => $"{bytes} bytes"
		};
	}

	private static void CollectBakes(string directory, string pattern, string kind, List<(string, string, string)> into) {
		if (!Directory.Exists(directory)) return;

		foreach (var path in Directory.EnumerateFiles(directory, pattern, SearchOption.TopDirectoryOnly)) {
			var uid = System.IO.Path.GetFileNameWithoutExtension(path);
			if (!string.IsNullOrEmpty(uid)) into.Add((path, uid, kind));
		}
	}

	/// <summary>Which of <paramref name="candidates" />' uids appear in any scene or prefab in the project.</summary>
	private static HashSet<string> CollectReferencedUids(List<(string Path, string Uid, string Kind)> candidates) {
		var referenced = new HashSet<string>(StringComparer.Ordinal);

		// Decoded up front: matching raw bytes per file is one pass over each scene rather than one per uid
		var patterns = candidates
			.Select(c => (c.Uid, Bytes: TryDecodeUid(c.Uid)))
			.Where(p => p.Bytes is not null)
			.ToList();

		foreach (var root in ProjectContext.DatabaseRoots) {
			if (!Directory.Exists(root)) continue;

			foreach (var file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)) {
				if (!s_sceneExtensions.Contains(System.IO.Path.GetExtension(file), StringComparer.OrdinalIgnoreCase))
					continue;

				byte[] contents;
				try {
					contents = File.ReadAllBytes(file);
				}
				catch (Exception) {
					// Unreadable scene: treat every uid as possibly referenced by it rather than risk deleting
					// a bake this file was the only user of
					foreach (var (uid, _) in patterns) referenced.Add(uid);
					continue;
				}

				foreach (var (uid, bytes) in patterns) {
					if (referenced.Contains(uid)) continue;
					if (ContainsSequence(contents, bytes!) || ContainsText(contents, uid)) referenced.Add(uid);
				}
			}
		}

		return referenced;
	}

	/// <summary>Base64url back to the little-endian 8 bytes a scene stores, or null if not a uid.</summary>
	private static byte[]? TryDecodeUid(string uid) {
		try {
			var padded = uid.Replace('-', '+').Replace('_', '/');
			padded += new string('=', (4 - (padded.Length % 4)) % 4);
			var decoded = Convert.FromBase64String(padded);
			return decoded.Length == 8 ? decoded : null;
		}
		catch (FormatException) {
			return null;
		}
	}

	private static bool ContainsSequence(byte[] haystack, byte[] needle) {
		if (needle.Length == 0 || haystack.Length < needle.Length) return false;

		for (var i = 0; i <= haystack.Length - needle.Length; i++) {
			var match = true;
			for (var j = 0; j < needle.Length; j++)
				if (haystack[i + j] != needle[j]) {
					match = false;
					break;
				}

			if (match) return true;
		}

		return false;
	}

	private static bool ContainsText(byte[] haystack, string needle) {
		var bytes = System.Text.Encoding.UTF8.GetBytes(needle);
		return ContainsSequence(haystack, bytes);
	}
}
