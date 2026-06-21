using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace editor.Assets;

// two databases because they serve different purposes and have different lifetimes:
//   artwork_database.json  -> maps source artwork paths to output UIDs + hash
//                             used to skip re-importing files that havent changed
//   database.json          -> maps UID to virtual path + asset type
//                             full rebuild on every import, used by the engine at runtime
public static class AssetDatabase {
	private static readonly JsonSerializerOptions s_prettyJson = new() { WriteIndented = true };

	public static string ComputeHash(string realPath) {
		using var stream = File.OpenRead(realPath);
		var hash = SHA256.HashData(stream);
		return "sha256:" + Convert.ToHexString(hash).ToLowerInvariant();
	}

	public static bool IsUpToDate(string sourceVirtualPath, string hash) {
		var db = LoadArtworkDatabase();
		return db[sourceVirtualPath] is JsonObject entry
		       && entry["last_hash"]?.GetValue<string>() == hash;
	}

	public static void UpdateArtworkDatabase(
		string sourceVirtualPath, string hash, IEnumerable<string> outputUids) {
		var db = LoadArtworkDatabase();

		var outputs = new JsonArray();
		foreach (var uid in outputUids) outputs.Add(uid);

		db[sourceVirtualPath] = new JsonObject {
			["last_hash"] = hash,
			["outputs"] = outputs
		};

		SaveArtworkDatabase(db);
	}

	// full re-scan of all .meta files -> rewrites database.json from scratch
	// slow but simple, only called after import completes (not on every change)
	public static void RebuildAssetDatabase() {
		var db = new JsonObject {
			["version"] = 2,
			["generated_at"] = DateTime.UtcNow.ToString("o")
		};

		ScanDirectory(ProjectContext.AssetsPath, db);
		ScanDirectory(ProjectContext.CorePath, db);

		var path = ProjectContext.Resolve("cache://database.json");
		File.WriteAllText(path, db.ToJsonString(s_prettyJson));
	}

	// collections are created on demand
	private static void ScanDirectory(string directory, JsonObject db) {
		if (!Directory.Exists(directory)) return;
		foreach (var metaPath in MetaFile.FindAll(directory)) {
			var header = MetaFile.ReadHeader(metaPath);
			if (header is null) continue;

			var assetRealPath = metaPath[..^5]; // strip ".meta"
			var virtualPath = ProjectContext.ToVirtual(assetRealPath);
			if (virtualPath is null) continue;

			if (db[header.VectorName] is not JsonObject collection) {
				collection = new JsonObject();
				db[header.VectorName] = collection;
			}

			collection[header.Uid] = virtualPath;
		}
	}

	private static JsonObject LoadArtworkDatabase() {
		var path = ProjectContext.Resolve("cache://artwork_database.json");
		if (!File.Exists(path))
			return new JsonObject {
				["version"] = 1,
				["type"] = "artwork_database"
			};
		try {
			return JsonNode.Parse(File.ReadAllText(path)) as JsonObject
			       ?? new JsonObject { ["version"] = 1, ["type"] = "artwork_database" };
		} catch {
			return new JsonObject { ["version"] = 1, ["type"] = "artwork_database" };
		}
	}

	private static void SaveArtworkDatabase(JsonObject db) {
		var path = ProjectContext.Resolve("cache://artwork_database.json");
		File.WriteAllText(path, db.ToJsonString(s_prettyJson));
	}
}
