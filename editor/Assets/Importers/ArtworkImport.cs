//
// ArtworkImport.cs by Xein
// 

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace editor.Assets.Importers;

public sealed record ArtworkOutput(string Uid, string Name, string? Hash, string Uri);

public static class ArtworkImport {
	public static async Task<IReadOnlyList<ArtworkOutput>> RunAsync(
		IAssetImporter importer, string realSource, string sourceVirtual, string destDir,
		IReadOnlyList<ArtworkOutput> previous, Action<string> log, Action<double>? progress = null) {
		var writes = new List<(string Uid, string Path)>();
		using var scope = MetaFile.Capture(writes);

		if (previous.Count == 0) {
			var ctx = new ImportContext { DestDir = destDir, SourceVirtualPath = sourceVirtual };
			await importer.Import(realSource, ctx, log, progress);
			return Describe(Produced(writes, null));
		}

		var stageRoot = Path.Combine(ProjectContext.CachePath, "import_staging", Guid.NewGuid().ToString("N"));
		// Named like the real folder
		var stageDest = Path.Combine(stageRoot, new DirectoryInfo(destDir).Name);
		Directory.CreateDirectory(stageDest);
		try {
			var ctx = new ImportContext { DestDir = stageDest, SourceVirtualPath = sourceVirtual };
			await importer.Import(realSource, ctx, log, progress);
			return Reconcile(Produced(writes, stageRoot), previous, stageDest, destDir, log);
		} finally {
			try {
				Directory.Delete(stageRoot, true);
			} catch {
				/* the cache is disposable */
			}
		}
	}

	private static List<(string Uid, string Path)> Produced(List<(string Uid, string Path)> writes, string? root) {
		var byUid = new Dictionary<string, string>();
		foreach (var (uid, path) in writes) byUid[uid] = path;
		return byUid
			.Where(kv => File.Exists(kv.Value) && (root is null || kv.Value.StartsWith(root, StringComparison.OrdinalIgnoreCase)))
			.Select(kv => (kv.Key, kv.Value))
			.ToList();
	}

	private static IReadOnlyList<ArtworkOutput> Describe(IEnumerable<(string Uid, string Path)> produced) {
		return produced.Select(p => Describe(p.Uid, p.Path)).ToList();
	}

	private static ArtworkOutput Describe(string uid, string realPath) {
		return new ArtworkOutput(uid, Path.GetFileName(realPath), AssetDatabase.ComputeHash(realPath),
			ProjectContext.ToVirtual(realPath) ?? realPath);
	}

	private static IReadOnlyList<ArtworkOutput> Reconcile(
		List<(string Uid, string Path)> produced, IReadOnlyList<ArtworkOutput> previous,
		string stageDest, string destDir, Action<string> log) {
		var fresh = produced.Select(p => (p.Uid, p.Path, Name: Path.GetFileName(p.Path),
			Hash: AssetDatabase.ComputeHash(p.Path))).ToList();

		var free = previous.ToList();
		var matches = new Dictionary<string, ArtworkOutput>();

		void Match(Func<ArtworkOutput, (string Uid, string Path, string Name, string Hash), bool> same) {
			foreach (var f in fresh) {
				if (matches.ContainsKey(f.Uid)) continue;
				var old = free.FirstOrDefault(o => same(o, f));
				if (old is null) continue;
				matches[f.Uid] = old;
				free.Remove(old);
			}
		}

		Match((o, f) => o.Hash == f.Hash);
		Match((o, f) => string.Equals(o.Name, f.Name, StringComparison.OrdinalIgnoreCase));

		foreach (var f in fresh) {
			if (matches.ContainsKey(f.Uid)) continue;
			var old = free.FirstOrDefault(o => o.Hash is null);
			if (old is null) break;
			matches[f.Uid] = old;
			free.Remove(old);
		}

		var uidMap = matches.ToDictionary(m => m.Key, m => m.Value.Uid);
		if (uidMap.Count > 0)
			foreach (var file in Directory.EnumerateFiles(stageDest, "*", SearchOption.AllDirectories))
				ReplaceUids(file, uidMap);

		var outputs = new List<ArtworkOutput>();
		var moved = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		foreach (var f in fresh) {
			string target;
			if (matches.TryGetValue(f.Uid, out var old)) {
				var uri = AssetDatabase.TryResolve(old.Uid, out var current, out _) ? current : old.Uri;
				target = ProjectContext.Resolve(uri);
			} else {
				target = Path.Combine(destDir, Path.GetRelativePath(stageDest, f.Path));
			}

			var tags = MetaFile.ReadHeader(target)?.Tags;
			Place(f.Path, target);
			Place(f.Path + ".meta", target + ".meta");
			if (tags is { Count: > 0 }) MetaFile.SetTags(target, tags);

			moved.Add(f.Path);
			moved.Add(f.Path + ".meta");
			var uid = matches.TryGetValue(f.Uid, out var kept) ? kept.Uid : f.Uid;
			outputs.Add(Describe(uid, target));
			log($"{(kept is null ? "New" : "Replaced")} {ProjectContext.ToVirtual(target) ?? target}");
		}

		foreach (var file in Directory.EnumerateFiles(stageDest, "*", SearchOption.AllDirectories))
			if (!moved.Contains(file))
				Place(file, Path.Combine(destDir, Path.GetRelativePath(stageDest, file)));

		return outputs;
	}

	private static void Place(string from, string to) {
		if (!File.Exists(from)) return;
		Directory.CreateDirectory(Path.GetDirectoryName(to)!);
		File.Move(from, to, true);
	}

	private static void ReplaceUids(string file, IReadOnlyDictionary<string, string> uidMap) {
		var bytes = File.ReadAllBytes(file);
		if (Array.IndexOf(bytes, (byte)0, 0, Math.Min(bytes.Length, 8192)) >= 0) return;

		var text = System.Text.Encoding.UTF8.GetString(bytes);
		var changed = text;
		foreach (var (from, to) in uidMap) changed = changed.Replace(from, to, StringComparison.Ordinal);
		if (changed != text) File.WriteAllText(file, changed);
	}
}
