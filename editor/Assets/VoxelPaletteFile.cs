//
// VoxelPaletteFile.cs
// 12 Sep 2026
//

using System;
using System.Collections.Generic;
using System.IO;
using Tomlyn;
using Tomlyn.Model;

namespace editor.Assets;

public sealed class VoxelPaletteEntry {
	public bool InUse { get; set; }

	public byte R { get; set; }
	public byte G { get; set; }
	public byte B { get; set; }

	public float Roughness { get; set; }
	public float Metallic { get; set; }
	public float Reflectivity { get; set; }
	public float Emissive { get; set; }

	public int Material { get; set; }

	/// <summary>Authored without a material which differs from material 0</summary>
	public bool HasMaterial { get; set; }

	public int TransformsTo { get; set; }
	public bool Transparent { get; set; }
}

/// <summary>Mirrors the assets::VoxelPalette TOML shape</summary>
public sealed class VoxelPaletteFile {
	public const int Size = 256;

	public string LibraryUid { get; set; } = "";

	public float MaxEmissive { get; set; } = 1f;

	public VoxelPaletteEntry[] Entries { get; } = CreateEntries();

	private static VoxelPaletteEntry[] CreateEntries() {
		var entries = new VoxelPaletteEntry[Size];
		for (var i = 0; i < Size; i++) entries[i] = new VoxelPaletteEntry();
		return entries;
	}

	public static VoxelPaletteFile FromFile(string path) {
		return FromString(File.ReadAllText(path));
	}

	public static VoxelPaletteFile FromString(string toml) {
		var table = TomlSerializer.Deserialize<TomlTable>(toml)
			?? throw new FormatException("Voxel palette: failed to parse TOML");

		var palette = new VoxelPaletteFile {
			LibraryUid = GetString(table, "library", ""),
			MaxEmissive = GetFloat(table, "max_emissive", 1f)
		};

		if (!table.TryGetValue("entries", out var list) || list is not TomlTableArray entries) return palette;

		foreach (var entry in entries) {
			var index = GetInt(entry, "index", 0);
			if (index is < 1 or >= Size) continue;    // index 0 is the empty voxel

			var target = palette.Entries[index];
			target.InUse = true;

			if (entry.TryGetValue("albedo", out var albedo) && albedo is TomlArray rgb && rgb.Count == 3) {
				target.R = ToByte(rgb[0]);
				target.G = ToByte(rgb[1]);
				target.B = ToByte(rgb[2]);
			}

			target.Roughness = GetFloat(entry, "roughness", 0f);
			target.Metallic = GetFloat(entry, "metallic", 0f);
			target.Reflectivity = GetFloat(entry, "reflectivity", 0f);
			target.Emissive = GetFloat(entry, "emissive", 0f);

			target.HasMaterial = entry.ContainsKey("material");
			target.Material = GetInt(entry, "material", 0);

			target.TransformsTo = GetInt(entry, "transforms_to", 0);
			target.Transparent = entry.TryGetValue("transparent", out var transparent) && transparent is true;
		}

		return palette;
	}

	public void Save(string path) {
		File.WriteAllText(path, Serialize());
	}

	public string Serialize() {
		var root = new TomlTable();
		if (LibraryUid.Length > 0) root["library"] = LibraryUid;
		root["max_emissive"] = (double)MaxEmissive;

		var list = new TomlTableArray();
		for (var index = 1; index < Size; index++) {
			var entry = Entries[index];
			if (!entry.InUse) continue;

			var table = new TomlTable {
				["index"] = (long)index,
				["albedo"] = new TomlArray { (long)entry.R, (long)entry.G, (long)entry.B },
				["roughness"] = (double)Clamp01(entry.Roughness),
				["metallic"] = (double)Clamp01(entry.Metallic),
				["reflectivity"] = (double)Clamp01(entry.Reflectivity),
				["emissive"] = (double)Clamp01(entry.Emissive)
			};
			if (entry.HasMaterial) table["material"] = (long)Math.Clamp(entry.Material, 0, 255);
			table["transforms_to"] = (long)Math.Clamp(entry.TransformsTo, 0, 255);
			table["transparent"] = entry.Transparent;
			list.Add(table);
		}

		root["entries"] = list;
		return TomlSerializer.Serialize(root);
	}

	private static float Clamp01(float value) {
		return float.IsFinite(value) ? Math.Clamp(value, 0f, 1f) : 0f;
	}

	private static byte ToByte(object? value) {
		var number = value switch {
			long l => l,
			int i => i,
			double d => (long)Math.Round(d),
			_ => 0L
		};
		return (byte)Math.Clamp(number, 0, 255);
	}

	private static string GetString(TomlTable t, string key, string fallback) {
		return t.TryGetValue(key, out var v) && v is string s ? s : fallback;
	}

	private static int GetInt(TomlTable t, string key, int fallback) {
		return t.TryGetValue(key, out var v)
			? v switch { long l => (int)l, int i => i, double d => (int)d, _ => fallback }
			: fallback;
	}

	private static float GetFloat(TomlTable t, string key, float fallback) {
		return t.TryGetValue(key, out var v)
			? v switch { double d => (float)d, float f => f, long l => l, int i => i, _ => fallback }
			: fallback;
	}
}

public static class VoxelMaterialLibraryFile {
	public static List<string> LoadNames(string uid) {
		if (string.IsNullOrEmpty(uid)) return [];
		if (!AssetDatabase.TryResolve(uid, out var virtualPath, out _)) return [];

		try {
			var table = TomlSerializer.Deserialize<TomlTable>(File.ReadAllText(ProjectContext.Resolve(virtualPath)));
			if (table is null || !table.TryGetValue("materials", out var list) || list is not TomlTableArray materials)
				return [];

			var names = new List<string>(materials.Count);
			for (var i = 0; i < materials.Count; i++) {
				names.Add(materials[i].TryGetValue("name", out var name) && name is string text && text.Length > 0
					? text
					: $"material {i}");
			}
			return names;
		} catch {
			return [];
		}
	}
}
