//
// AssetStore.cs by Xein
// 8 Nov 2026
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using Microsoft.Data.Sqlite;

namespace editor.Assets;

public sealed record AssetRecord(
	string Uid, string Type, string Db, string Path,
	string? ArtworkId, string? OutputName, string? OutputHash,
	string CreatedAt, string? ModifiedAt) {
	public string Virtual => $"{Db}://{Path}";
	public bool IsCore => Db == AssetStore.CoreDb;
}

public sealed record ArtworkRecord(string Id, string Uri, string? Hash);

public static class AssetStore {
	public const string CoreDb = "core";
	public const string RuntimeDatabaseName = "game.db";
	private const int SchemaVersion = 2;
	private static readonly object s_lock = new();
	private static SqliteConnection? s_db;
	public static bool IsOpen => s_db is not null;

	public static void Open() {
		Close();
		var path = Path.Combine(ProjectContext.CachePath, "assets.db");
		Directory.CreateDirectory(Path.GetDirectoryName(path)!);

		lock (s_lock) {
			s_db = new SqliteConnection($"Data Source={path};Pooling=False");
			s_db.Open();
			Exec("PRAGMA journal_mode = WAL");
			Exec("PRAGMA foreign_keys = ON");
			Exec("PRAGMA synchronous = NORMAL");
			EnsureSchema();
		}
	}

	public static void Close() {
		lock (s_lock) {
			s_db?.Dispose();
			s_db = null;
		}
	}

	private static void EnsureSchema() {
		if (Scalar<long>("PRAGMA user_version") != SchemaVersion) {
			Exec("DROP TABLE IF EXISTS asset_tags");
			Exec("DROP TABLE IF EXISTS assets");
			Exec("DROP TABLE IF EXISTS artwork");
			Exec("DROP TABLE IF EXISTS meta");
		}

		Exec("""
		     CREATE TABLE IF NOT EXISTS artwork (
		       id   TEXT PRIMARY KEY,
		       uri  TEXT NOT NULL UNIQUE,
		       hash TEXT
		     )
		     """);
		Exec("""
		     CREATE TABLE IF NOT EXISTS assets (
		       uid         TEXT PRIMARY KEY,
		       type        TEXT NOT NULL,
		       db          TEXT NOT NULL,
		       path        TEXT NOT NULL,
		       artwork_id  TEXT REFERENCES artwork(id) ON DELETE SET NULL,
		       output_name TEXT,
		       output_hash TEXT,
		       created_at  TEXT NOT NULL,
		       modified_at TEXT,
		       meta_mtime  INTEGER NOT NULL DEFAULT 0,
		       UNIQUE (db, path)
		     )
		     """);
		Exec("CREATE INDEX IF NOT EXISTS assets_artwork ON assets(artwork_id)");
		Exec("CREATE INDEX IF NOT EXISTS assets_type ON assets(type)");
		Exec($"PRAGMA user_version = {SchemaVersion}");
	}

	public static bool Sync() {
		lock (s_lock) {
			if (s_db is null) return false;

			var known = Query("SELECT uid, db, path, meta_mtime FROM assets",
				r => (Uid: r.GetString(0), Virtual: $"{r.GetString(1)}://{r.GetString(2)}", Mtime: r.GetInt64(3)));
			var byVirtual = known.ToDictionary(k => k.Virtual, k => k, StringComparer.Ordinal);

			var seen = new HashSet<string>(StringComparer.Ordinal);
			var fresh = new List<(string Meta, string Virtual, long Mtime)>();
			foreach (var root in ProjectContext.DatabaseRoots.Append(ProjectContext.CorePath)) {
				if (!Directory.Exists(root)) continue;
				foreach (var meta in Directory.EnumerateFiles(root, "*.meta", SearchOption.AllDirectories)) {
					var virtualPath = ProjectContext.ToVirtual(meta[..^".meta".Length]);
					if (virtualPath is null || !TrySplit(virtualPath, out var scheme, out _) || !IsTrackedScheme(scheme)) continue;

					var mtime = File.GetLastWriteTimeUtc(meta).Ticks;
					if (byVirtual.TryGetValue(virtualPath, out var row) && row.Mtime == mtime) {
						seen.Add(row.Uid);
						continue;
					}

					fresh.Add((meta, virtualPath, mtime));
				}
			}

			var changed = false;
			using var tx = s_db.BeginTransaction();
			foreach (var (meta, virtualPath, mtime) in fresh) {
				var header = MetaFile.ReadHeader(meta);
				if (header is null || !TrySplit(virtualPath, out var db, out var path)) continue;

				string? artworkId = null;
				if (header.Source is { Length: > 0 } source) artworkId = UpsertArtworkRow(source);

				Exec("DELETE FROM assets WHERE db = @db AND path = @path AND uid <> @uid", ("@db", db), ("@path", path), ("@uid", header.Uid));
				Exec("""
				     INSERT INTO assets (uid, type, db, path, artwork_id, created_at, modified_at, meta_mtime)
				     VALUES (@uid, @type, @db, @path, @artwork, @created, @modified, @mtime)
				     ON CONFLICT(uid) DO UPDATE SET
				       type = excluded.type, db = excluded.db, path = excluded.path, artwork_id = excluded.artwork_id,
				       created_at = excluded.created_at, modified_at = excluded.modified_at, meta_mtime = excluded.meta_mtime
				     """,
					("@uid", header.Uid), ("@type", header.Type), ("@db", db), ("@path", path), ("@artwork", artworkId),
					("@created", header.CreatedAt), ("@modified", header.ModifiedAt), ("@mtime", mtime));
				seen.Add(header.Uid);
				changed = true;
			}

			foreach (var (uid, _, _) in known.Select(k => (k.Uid, k.Virtual, k.Mtime)))
				if (!seen.Contains(uid)) {
					Exec("DELETE FROM assets WHERE uid = @uid", ("@uid", uid));
					changed = true;
				}

			if (changed) Exec("DELETE FROM artwork WHERE id NOT IN (SELECT artwork_id FROM assets WHERE artwork_id IS NOT NULL)");
			tx.Commit();
			return changed;
		}
	}

	public static AssetRecord? ByUid(string uid) {
		lock (s_lock) {
			return s_db is null ? null : Query(AssetSelect + " WHERE uid = @uid", ReadAsset, ("@uid", uid)).FirstOrDefault();
		}
	}

	public static AssetRecord? ByVirtual(string virtualPath) {
		if (!TrySplit(virtualPath, out var db, out var path)) return null;
		lock (s_lock) {
			return s_db is null
				? null
				: Query(AssetSelect + " WHERE db = @db AND path = @path", ReadAsset, ("@db", db), ("@path", path)).FirstOrDefault();
		}
	}

	public static IReadOnlyList<AssetRecord> ListAll() {
		lock (s_lock) {
			return s_db is null ? [] : Query(AssetSelect + " ORDER BY db, path", ReadAsset);
		}
	}

	public static IReadOnlyList<AssetRecord> ListByType(string type) {
		lock (s_lock) {
			return s_db is null ? [] : Query(AssetSelect + " WHERE type = @type ORDER BY db, path", ReadAsset, ("@type", type));
		}
	}

	public static IReadOnlyList<AssetRecord> ListDirectory(string virtualDir) {
		if (!TrySplit(virtualDir, out var db, out var dir)) return [];
		dir = dir.Trim('/');
		lock (s_lock) {
			if (s_db is null) return [];
			var prefix = dir.Length == 0 ? "" : dir + "/";
			var rows = Query(AssetSelect + " WHERE db = @db AND path LIKE @like ESCAPE '\\'", ReadAsset,
				("@db", db), ("@like", EscapeLike(prefix) + "%"));
			return rows.Where(r => r.Path.IndexOf('/', prefix.Length) < 0).OrderBy(r => r.Path, StringComparer.OrdinalIgnoreCase).ToList();
		}
	}

	public static IReadOnlyList<AssetRecord> OutputsOf(string artworkUri) {
		lock (s_lock) {
			return s_db is null
				? []
				: Query(AssetSelect + " WHERE artwork_id = (SELECT id FROM artwork WHERE uri = @uri) ORDER BY path", ReadAsset, ("@uri", artworkUri));
		}
	}

	public static ArtworkRecord? ArtworkByUri(string uri) {
		lock (s_lock) {
			return s_db is null ? null : Query(ArtworkSelect + " WHERE uri = @uri", ReadArtwork, ("@uri", uri)).FirstOrDefault();
		}
	}

	public static IReadOnlyList<ArtworkRecord> AllArtwork() {
		lock (s_lock) {
			return s_db is null ? [] : Query(ArtworkSelect + " ORDER BY uri", ReadArtwork);
		}
	}

	public static string UpsertArtwork(string uri) {
		lock (s_lock) {
			return s_db is null ? "" : UpsertArtworkRow(uri);
		}
	}

	public static void SetArtworkHash(string uri, string? hash) {
		lock (s_lock) {
			if (s_db is null) return;
			Exec("UPDATE artwork SET hash = @hash WHERE uri = @uri", ("@hash", hash), ("@uri", uri));
		}
	}

	public static void SetOutputInfo(string uid, string? name, string? hash, string? artworkId = null) {
		lock (s_lock) {
			if (s_db is null) return;
			Exec("UPDATE assets SET output_name = @name, output_hash = @hash, artwork_id = COALESCE(@artwork, artwork_id) WHERE uid = @uid",
				("@name", name), ("@hash", hash), ("@artwork", artworkId), ("@uid", uid));
		}
	}

	public static void RenameArtwork(string oldUri, string newUri) {
		lock (s_lock) {
			if (s_db is null) return;
			var from = Query(ArtworkSelect + " WHERE uri = @uri", ReadArtwork, ("@uri", oldUri)).FirstOrDefault();
			if (from is null) return;
			if (Query(ArtworkSelect + " WHERE uri = @uri", ReadArtwork, ("@uri", newUri)).FirstOrDefault() is { } clash && clash.Id != from.Id) {
				Exec("UPDATE assets SET artwork_id = @to WHERE artwork_id = @from", ("@to", clash.Id), ("@from", from.Id));
				Exec("UPDATE artwork SET hash = COALESCE(hash, @hash) WHERE id = @id", ("@hash", from.Hash), ("@id", clash.Id));
				Exec("DELETE FROM artwork WHERE id = @id", ("@id", from.Id));
			} else {
				Exec("UPDATE artwork SET uri = @uri WHERE id = @id", ("@uri", newUri), ("@id", from.Id));
			}
		}
	}

	public static void DeleteArtwork(string uri) {
		lock (s_lock) {
			if (s_db is null) return;
			Exec("DELETE FROM artwork WHERE uri = @uri", ("@uri", uri));
		}
	}

	public static void UnlinkArtwork(IEnumerable<string> uids) {
		lock (s_lock) {
			if (s_db is null) return;
			foreach (var uid in uids)
				Exec("UPDATE assets SET artwork_id = NULL, output_name = NULL, output_hash = NULL WHERE uid = @uid", ("@uid", uid));
			Exec("DELETE FROM artwork WHERE id NOT IN (SELECT artwork_id FROM assets WHERE artwork_id IS NOT NULL)");
		}
	}

	private static string UpsertArtworkRow(string uri) {
		var existing = Query(ArtworkSelect + " WHERE uri = @uri", ReadArtwork, ("@uri", uri)).FirstOrDefault();
		if (existing is not null) return existing.Id;
		var id = UidGenerator.Generate();
		Exec("INSERT INTO artwork (id, uri, hash) VALUES (@id, @uri, NULL)", ("@id", id), ("@uri", uri));
		return id;
	}

	public static void ExportRuntimeDatabase(string path, IReadOnlyList<(string Db, string File)> paks) {
		var folder = Path.GetDirectoryName(Path.GetFullPath(path))!;
		Directory.CreateDirectory(folder);
		foreach (var suffix in new[] { "", "-wal", "-shm", "-journal" })
			if (File.Exists(path + suffix))
				File.Delete(path + suffix);

		var packed = paks.Select(p => p.Db).ToHashSet(StringComparer.Ordinal);
		var rows = ListAll().Where(a => packed.Contains(a.Db)).ToList();

		using (var db = new SqliteConnection($"Data Source={path};Pooling=False")) {
			db.Open();
			using var tx = db.BeginTransaction();

			void Run(string sql, params (string Name, object? Value)[] args) {
				using var cmd = db.CreateCommand();
				cmd.Transaction = tx;
				cmd.CommandText = sql;
				foreach (var (name, value) in args) cmd.Parameters.AddWithValue(name, value ?? DBNull.Value);
				cmd.ExecuteNonQuery();
			}

			Run("CREATE TABLE assets (uid TEXT PRIMARY KEY, type TEXT NOT NULL, db TEXT NOT NULL, path TEXT NOT NULL)");
			Run("CREATE TABLE paks (db TEXT PRIMARY KEY, file TEXT NOT NULL, size INTEGER, hash TEXT)");
			foreach (var row in rows)
				Run("INSERT INTO assets (uid, type, db, path) VALUES (@uid, @type, @db, @path)",
					("@uid", row.Uid), ("@type", row.Type), ("@db", row.Db), ("@path", row.Path));

			foreach (var (name, file) in paks) {
				var pakPath = Path.Combine(folder, file);
				long? size = null;
				string? hash = null;
				if (File.Exists(pakPath)) {
					size = new FileInfo(pakPath).Length;
					using var stream = File.OpenRead(pakPath);
					hash = Convert.ToHexString(SHA256.HashData(stream));
				}

				Run("INSERT INTO paks (db, file, size, hash) VALUES (@db, @file, @size, @hash)",
					("@db", name), ("@file", file.Replace('\\', '/')), ("@size", size), ("@hash", hash));
			}

			tx.Commit();
		}

		SqliteConnection.ClearAllPools();
	}

	private const string AssetSelect =
		"SELECT uid, type, db, path, artwork_id, output_name, output_hash, created_at, modified_at FROM assets";

	private const string ArtworkSelect = "SELECT id, uri, hash FROM artwork";

	private static AssetRecord ReadAsset(SqliteDataReader r) {
		return new AssetRecord(r.GetString(0), r.GetString(1), r.GetString(2), r.GetString(3),
			NullableString(r, 4), NullableString(r, 5), NullableString(r, 6), r.GetString(7), NullableString(r, 8));
	}

	private static ArtworkRecord ReadArtwork(SqliteDataReader r) {
		return new ArtworkRecord(r.GetString(0), r.GetString(1), NullableString(r, 2));
	}

	private static string? NullableString(SqliteDataReader r, int index) {
		return r.IsDBNull(index) ? null : r.GetString(index);
	}

	public static bool TrySplit(string virtualPath, out string db, out string path) {
		db = path = "";
		var sep = virtualPath.IndexOf("://", StringComparison.Ordinal);
		if (sep <= 0) return false;
		db = virtualPath[..sep];
		path = virtualPath[(sep + 3)..].Replace('\\', '/');
		return true;
	}

	public static bool IsTrackedScheme(string db) {
		return db == CoreDb || ProjectContext.Databases.Contains(db);
	}

	private static string EscapeLike(string text) {
		return text.Replace("\\", "\\\\").Replace("%", "\\%").Replace("_", "\\_");
	}

	private static SqliteCommand Command(string sql, (string Name, object? Value)[] args) {
		var cmd = s_db!.CreateCommand();
		cmd.CommandText = sql;
		foreach (var (name, value) in args) cmd.Parameters.AddWithValue(name, value ?? DBNull.Value);
		return cmd;
	}

	private static int Exec(string sql, params (string Name, object? Value)[] args) {
		using var cmd = Command(sql, args);
		return cmd.ExecuteNonQuery();
	}

	private static T? Scalar<T>(string sql, params (string Name, object? Value)[] args) {
		using var cmd = Command(sql, args);
		var value = cmd.ExecuteScalar();
		return value is null or DBNull ? default : (T)Convert.ChangeType(value, typeof(T));
	}

	private static List<T> Query<T>(string sql, Func<SqliteDataReader, T> map, params (string Name, object? Value)[] args) {
		using var cmd = Command(sql, args);
		using var reader = cmd.ExecuteReader();
		var rows = new List<T>();
		while (reader.Read()) rows.Add(map(reader));
		return rows;
	}
}
