using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace editor.Services;

// Manages the two JSON databases that track imported assets
//   cache://artwork_database.json: source file → output UIDs + hash
//   cache://database.json        : uid → asset path + type
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

	// Full re-scan of all .meta files → rewrites cache://database.json
	public static void RebuildAssetDatabase() {
		var assets = new JsonObject();

		ScanDirectory(ProjectContext.AssetsPath, assets);
		ScanDirectory(ProjectContext.CorePath, assets);

		var db = new JsonObject {
			["version"] = 1,
			["generated_at"] = DateTime.UtcNow.ToString("o"),
			["assets"] = assets
		};

		var path = ProjectContext.Resolve("cache://database.json");
		File.WriteAllText(path, db.ToJsonString(s_prettyJson));
	}

	private static void ScanDirectory(string directory, JsonObject assets) {
		if (!Directory.Exists(directory)) return;
		foreach (var metaPath in MetaFile.FindAll(directory)) {
			var meta = MetaFile.ReadTexture(metaPath);
			if (meta is null) continue;

			// meta lives at "foo.ktx2.meta", asset is "foo.ktx2"
			var assetRealPath = metaPath[..^5]; // strip ".meta"
			var virtualPath = ProjectContext.ToVirtual(assetRealPath);
			if (virtualPath is null) continue;

			assets[meta.Uid] = new JsonObject {
				["path"] = virtualPath,
				["type"] = meta.Type
			};
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
