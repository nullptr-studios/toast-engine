using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Tomlyn;
using Tomlyn.Model;

namespace editor.Assets;

public sealed class DetailTextureEntry {
	public int Id { get; set; }
	public string Name { get; set; } = "";
	public string AlbedoUid { get; set; } = "";
	public string RoughnessUid { get; set; } = "";
	public string HeightUid { get; set; } = "";
	public DetailTextureEntry Clone() => new() { Id = Id, Name = Name, AlbedoUid = AlbedoUid, RoughnessUid = RoughnessUid, HeightUid = HeightUid };
}

public sealed class DetailTextureFile {
	public const int MaxTextures = 255;
	public List<DetailTextureEntry> Entries { get; } = [];

	public static DetailTextureFile FromFile(string path) => FromString(File.ReadAllText(path));

	public static DetailTextureFile FromString(string toml) {
		var table = TomlSerializer.Deserialize<TomlTable>(toml)
			?? throw new FormatException("Detail texture: failed to parse TOML");

		var file = new DetailTextureFile();
		if (!table.TryGetValue("textures", out var list) || list is not TomlTableArray entries) return file;

		var seen = new HashSet<int>();
		foreach (var entry in entries) {
			var id = entry.TryGetValue("id", out var v) ? v switch { long l => (int)l, int i => i, _ => 0 } : 0;
			if (id is < 1 or > MaxTextures || !seen.Add(id)) continue;

			file.Entries.Add(new DetailTextureEntry {
				Id = id,
				Name = entry.TryGetValue("name", out var name) && name is string n ? n : "",
				AlbedoUid = entry.TryGetValue("albedo", out var albedo) && albedo is string a ? a : "",
				RoughnessUid = entry.TryGetValue("roughness", out var roughness) && roughness is string r ? r : "",
				HeightUid = entry.TryGetValue("height", out var height) && height is string h ? h : ""
			});
		}

		return file;
	}

	public int NextFreeId() {
		var used = Entries.Select(e => e.Id).ToHashSet();
		for (var id = 1; id <= MaxTextures; id++)
			if (!used.Contains(id)) return id;
		return 0;
	}

	public string Serialize() {
		var list = new TomlTableArray();
		foreach (var entry in Entries) {
			var table = new TomlTable {
				["id"] = (long)Math.Clamp(entry.Id, 1, MaxTextures),
				["name"] = entry.Name
			};
			if (entry.AlbedoUid.Length > 0) table["albedo"] = entry.AlbedoUid;
			if (entry.RoughnessUid.Length > 0) table["roughness"] = entry.RoughnessUid;
			if (entry.HeightUid.Length > 0) table["height"] = entry.HeightUid;
			list.Add(table);
		}

		return TomlSerializer.Serialize(new TomlTable { ["textures"] = list });
	}

	public void Save(string path) => File.WriteAllText(path, Serialize());
}
