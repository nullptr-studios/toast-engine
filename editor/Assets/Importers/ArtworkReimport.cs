using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace editor.Assets.Importers;

internal enum MissingAssetAction { Keep, Replace, Delete }
internal sealed record MissingAssetResolution(MissingAssetAction Action, ReimportAsset? Replacement = null);
internal sealed record ReimportAsset(string Uid, string Path, string Name, string Type, string Label);

internal sealed class ArtworkReimport {
	internal static readonly StringComparer PathComparer =
		OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal;

	private static readonly Regex s_nodeUid = new(
		@"^(m_uid|m_parent) @\S+ = ([A-Za-z0-9_-]{11})(?=\r?$)", RegexOptions.Multiline);
	private readonly List<Match> m_matches = [];
	private readonly Dictionary<ReimportAsset, (DateTime Asset, DateTime Meta)> m_originalTimes;
	private readonly string m_destDir;

	private sealed record Match(ReimportAsset Old, ReimportAsset New, bool SameContent, string Destination);

	public ArtworkReimport(IReadOnlyList<ArtworkOutput> previous, IReadOnlyList<(string Uid, string Path)> generated,
		string stageDest, string destDir) {
		m_destDir = destDir;
		var old = previous.Select(output => ReadAsset(output.Uid, ProjectContext.Resolve(output.Uri), output.Name,
			output.Uri)).ToList();
		var fresh = generated.Select(output => ReadAsset(output.Uid, output.Path, Path.GetFileName(output.Path),
			Path.GetRelativePath(stageDest, output.Path))).OrderBy(asset => asset.Label, PathComparer).ToList();
		var storedHashes = previous.ToDictionary(asset => asset.Uid, asset => asset.Hash);
		m_originalTimes = old.ToDictionary(asset => asset, asset => LastWriteTimes(asset.Path));
		var oldHashes = old.ToDictionary(asset => asset, asset => File.Exists(asset.Path) ? CompareHash(asset.Path) : null);
		var uidMap = new Dictionary<string, string>();

		// Resolve references to earlier outputs before comparing
		foreach (var group in fresh.GroupBy(asset => DependencyOrder(asset.Path)).OrderBy(group => group.Key)) {
			foreach (var asset in group) RemapReferences(asset.Path, uidMap);
			var hashes = group.ToDictionary(asset => asset, asset => CompareHash(asset.Path));

			bool SameName(ReimportAsset previousAsset, ReimportAsset generatedAsset) =>
				string.Equals(previousAsset.Name, generatedAsset.Name, StringComparison.OrdinalIgnoreCase);

			bool SamePath(ReimportAsset previousAsset, ReimportAsset generatedAsset) =>
				PathComparer.Equals(previousAsset.Path, GeneratedDestination(generatedAsset));

			bool SameHash(ReimportAsset previousAsset, ReimportAsset generatedAsset) =>
				oldHashes[previousAsset] == hashes[generatedAsset] ||
				storedHashes[previousAsset.Uid] == hashes[generatedAsset];

			void MatchAssets(Func<ReimportAsset, ReimportAsset, bool> predicate) {
				foreach (var asset in group) {
					if (uidMap.ContainsKey(asset.Uid)) continue;
					var candidates = old.Where(candidate => candidate.Type == asset.Type && predicate(candidate, asset));
					var match = candidates.FirstOrDefault(candidate => SamePath(candidate, asset)) ??
						candidates.FirstOrDefault();
					if (match is null) continue;
					old.Remove(match);
					uidMap.Add(asset.Uid, match.Uid);

					var sameContent = SameHash(match, asset) && File.Exists(match.Path);
					var rename = SameHash(match, asset) &&
						!string.Equals(match.Name, asset.Name, StringComparison.Ordinal);
					var destination = rename ? Path.Combine(Path.GetDirectoryName(match.Path)!, asset.Name) : match.Path;
					m_matches.Add(new Match(match, asset, sameContent, destination));
				}
			}

			// Prefer a matching name among identical
			MatchAssets((previousAsset, generatedAsset) => SameHash(previousAsset, generatedAsset) &&
				SamePath(previousAsset, generatedAsset));
			MatchAssets((previousAsset, generatedAsset) => SameHash(previousAsset, generatedAsset) &&
				SameName(previousAsset, generatedAsset));
			MatchAssets(SameHash);
			MatchAssets(SamePath);
			MatchAssets(SameName);
		}

		Added = fresh.Where(asset => !uidMap.ContainsKey(asset.Uid)).ToList();
		Missing = old;
	}

	public IReadOnlyList<ReimportAsset> Added { get; }
	public IReadOnlyList<ReimportAsset> Missing { get; }
	public int ModifiedCount => m_matches.Count(match => !match.SameContent ||
		!string.Equals(match.Old.Path, match.Destination, StringComparison.Ordinal));

	public IReadOnlyList<ArtworkOutput> Apply(IReadOnlyDictionary<string, MissingAssetResolution> decisions,
		string stageRoot, Action<string> log) {
		var matches = new List<Match>(m_matches);
		var added = new List<ReimportAsset>(Added);
		var removed = new List<ReimportAsset>();
		var replacements = new Dictionary<string, string>();
		foreach (var asset in Missing) {
			var resolution = decisions[asset.Uid];
			if (resolution.Action == MissingAssetAction.Keep) continue;
			if (resolution.Action == MissingAssetAction.Delete) {
				removed.Add(asset);
				continue;
			}

			var replacement = resolution.Replacement!;
			if (asset.Type != replacement.Type || !added.Remove(replacement))
				throw new InvalidOperationException($"Invalid replacement for {asset.Label}.");
			replacements.Add(replacement.Uid, asset.Uid);
			var destination = Path.Combine(Path.GetDirectoryName(asset.Path)!, replacement.Name);
			matches.Add(new Match(asset, replacement, false, destination));
		}

		// keep the missing asset's UID
		foreach (var asset in matches.Select(match => match.New).Concat(added))
			RemapReferences(asset.Path, replacements);

		var placements = new List<(string From, string To)>();
		var outputs = new List<ArtworkOutput>();
		var thumbnails = new List<(string From, string To)>();
		foreach (var match in matches) {
			var previous = VerifyUnchanged(match.Old);

			var sameContent = match.SameContent && (replacements.Count == 0 ||
				CompareHash(match.New.Path) == CompareHash(match.Old.Path));
			if (sameContent && string.Equals(match.Old.Path, match.Destination, StringComparison.Ordinal)) {
				outputs.Add(DescribeDestination(match.Old.Uid, match.New, match.Destination));
				continue;
			}

			var source = match.New.Path;
			if (sameContent) {
				placements.Add((match.Old.Path, match.Destination));
				placements.Add((match.Old.Path + ".meta", match.Destination + ".meta"));
			} else {
				MetaFile.ReuseIdentity(source, previous);
				placements.Add((source, match.Destination));
				placements.Add((source + ".meta", match.Destination + ".meta"));
				thumbnails.Add((match.New.Uid, match.Old.Uid));
			}

			removed.Add(match.Old);
			outputs.Add(DescribeDestination(match.Old.Uid, match.New, match.Destination));
		}

		foreach (var asset in added) {
			var target = GeneratedDestination(asset);
			placements.Add((asset.Path, target));
			placements.Add((asset.Path + ".meta", target + ".meta"));
			outputs.Add(DescribeDestination(asset.Uid, asset, target));
		}

		foreach (var asset in removed) VerifyUnchanged(asset);

		PlaceFiles(placements, removed.SelectMany(asset => new[] { asset.Path, asset.Path + ".meta" }), stageRoot);
		foreach (var (from, to) in thumbnails) {
			if (from == to) continue;
			var thumbnail = ArtworkImport.ThumbnailPath(from);
			var target = ArtworkImport.ThumbnailPath(to);
			try {
				AssetDatabase.TryDelete(target);
				if (File.Exists(thumbnail)) File.Move(thumbnail, target);
			} catch (IOException ex) {
				log($"Could not update thumbnail for {to}: {ex.Message}");
			} catch (UnauthorizedAccessException ex) {
				log($"Could not update thumbnail for {to}: {ex.Message}");
			}
		}

		foreach (var asset in Missing.Where(asset => decisions[asset.Uid].Action == MissingAssetAction.Delete))
			AssetDatabase.TryDelete(ArtworkImport.ThumbnailPath(asset.Uid));
		return outputs;
	}

	private static void PlaceFiles(IReadOnlyList<(string From, string To)> placements,
		IEnumerable<string> removals, string stageRoot) {
		var existing = removals.Where(File.Exists).ToHashSet(PathComparer);
		var destinations = new HashSet<string>(PathComparer);
		foreach (var (_, to) in placements) {
			if (!destinations.Add(to) || (File.Exists(to) && !existing.Contains(to)) || Directory.Exists(to))
				throw new IOException($"Reimport would overwrite an asset at {ProjectContext.ToVirtual(to) ?? to}.");
		}

		var backups = new Dictionary<string, string>(PathComparer);
		var installed = new List<string>();
		try {
			foreach (var path in existing) {
				var backup = Path.Combine(stageRoot, $"backup-{backups.Count}");
				File.Move(path, backup);
				backups.Add(path, backup);
			}

			foreach (var (from, to) in placements) {
				Directory.CreateDirectory(Path.GetDirectoryName(to)!);
				using var input = File.OpenRead(backups.GetValueOrDefault(from, from));
				using var output = File.Open(to, FileMode.CreateNew, FileAccess.Write);
				installed.Add(to);
				input.CopyTo(output);
			}
		} catch (Exception failure) {
			try {
				foreach (var path in installed) {
					File.SetAttributes(path, File.GetAttributes(path) & ~FileAttributes.ReadOnly);
					File.Delete(path);
				}
				foreach (var (path, backup) in backups) File.Move(backup, path);
			} catch (Exception restoreFailure) {
				throw new AggregateException($"Could not restore reimport files. Backups are in {stageRoot}.",
					failure, restoreFailure);
			}

			throw;
		}
	}

	private string GeneratedDestination(ReimportAsset asset) => Path.Combine(m_destDir, asset.Label);

	private static (DateTime Asset, DateTime Meta) LastWriteTimes(string path) =>
		(File.GetLastWriteTimeUtc(path), File.GetLastWriteTimeUtc(path + ".meta"));

	private MetaHeader VerifyUnchanged(ReimportAsset asset) {
		var header = MetaFile.ReadHeader(asset.Path);
		if (header?.Uid != asset.Uid || LastWriteTimes(asset.Path) != m_originalTimes[asset])
			throw new IOException($"Asset changed during reimport: {asset.Label}.");
		return header;
	}

	private static ReimportAsset ReadAsset(string uid, string path, string name, string label) =>
		new(uid, path, name, MetaFile.ReadHeader(path)?.Type ??
			throw new IOException($"Missing asset metadata for {label}."), label);

	private static ArtworkOutput DescribeDestination(string uid, ReimportAsset generated, string destination) =>
		new(uid, generated.Name, AssetDatabase.ComputeHash(generated.Path),
			ProjectContext.ToVirtual(destination) ?? destination);

	private static int DependencyOrder(string path) => Path.GetExtension(path).ToLowerInvariant() switch {
		".tnode" => 2,
		".tmat" or ".tvox" => 1,
		_ => 0
	};

	private static string CompareHash(string path) {
		if (!path.EndsWith(".tnode", StringComparison.OrdinalIgnoreCase)) return AssetDatabase.ComputeHash(path);

		// Compare their topology
		var text = File.ReadAllText(path).ReplaceLineEndings("\n");
		var ids = new Dictionary<string, int>();
		foreach (System.Text.RegularExpressions.Match match in s_nodeUid.Matches(text))
			if (match.Groups[1].Value == "m_uid") ids.TryAdd(match.Groups[2].Value, ids.Count);
		text = s_nodeUid.Replace(text, match => ids.TryGetValue(match.Groups[2].Value, out var index)
			? match.Groups[1].Value + " = " + index
			: match.Value);
		return "sha256:" + Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text))).ToLowerInvariant();
	}

	private static void RemapReferences(string path, IReadOnlyDictionary<string, string> uidMap) {
		if (uidMap.Count == 0) return;
		switch (Path.GetExtension(path).ToLowerInvariant()) {
			case ".tnode":
			case ".tmat": {
				var original = File.ReadAllText(path);
				var updated = original;
				foreach (var (from, to) in uidMap)
					updated = updated.Replace(from, to, StringComparison.Ordinal);
				if (updated != original) File.WriteAllText(path, updated);
				break;
			}
			case ".tvox": {
				const int paletteOffset = 16;
				using var stream = File.Open(path, FileMode.Open, FileAccess.ReadWrite);
				Span<byte> uidBytes = stackalloc byte[8];
				stream.Position = paletteOffset;
				stream.ReadExactly(uidBytes);
				var uid = UidGenerator.EncodeNative(BinaryPrimitives.ReadUInt64LittleEndian(uidBytes));
				if (!uidMap.TryGetValue(uid, out var target) || uid == target) break;
				var nativeBytes = Convert.FromBase64String(target.Replace('-', '+').Replace('_', '/') + "=");
				BinaryPrimitives.WriteUInt64LittleEndian(uidBytes, BinaryPrimitives.ReadUInt64BigEndian(nativeBytes));
				stream.Position = paletteOffset;
				stream.Write(uidBytes);
				break;
			}
		}
	}
}
