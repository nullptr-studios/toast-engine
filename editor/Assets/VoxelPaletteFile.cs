//
// VoxelPaletteFile.cs
// 12 Sep 2026
//

using System;
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

	public float Alpha { get; set; } = 1f;

	public int Material { get; set; }

	/// <summary>Authored without a material which differs from material 0</summary>
	public bool HasMaterial { get; set; }

	public int TransformsTo { get; set; }
	public bool Transparent { get; set; }
}

public sealed class VoxelMaterialSlot {
	public string Name { get; set; } = "";

	public string PhysicsUid { get; set; } = "";

	public string DestructionUid { get; set; } = "";

	public string ImpactSoundUid { get; set; } = "";
	public byte[] DustColour { get; set; } = [128, 128, 128];
	public string Tag { get; set; } = "";

	public bool IsEmpty =>
		Name.Length == 0 && PhysicsUid.Length == 0 && DestructionUid.Length == 0;
}

/// <summary>Mirrors the assets::VoxelPalette TOML shape</summary>
public sealed class VoxelPaletteFile {
	public const int Size = 256;

	public const int SlotCount = 8;

	public float MaxEmissive { get; set; } = 1f;

	public VoxelPaletteEntry[] Entries { get; } = CreateEntries();

	public VoxelMaterialSlot[] Slots { get; } = CreateSlots();

	private static VoxelPaletteEntry[] CreateEntries() {
		var entries = new VoxelPaletteEntry[Size];
		for (var i = 0; i < Size; i++) entries[i] = new VoxelPaletteEntry();
		return entries;
	}

	private static VoxelMaterialSlot[] CreateSlots() {
		var slots = new VoxelMaterialSlot[SlotCount];
		for (var i = 0; i < SlotCount; i++) slots[i] = new VoxelMaterialSlot();
		return slots;
	}

	public static VoxelPaletteFile FromFile(string path) {
		return FromString(File.ReadAllText(path));
	}

	public static VoxelPaletteFile FromString(string toml) {
		var table = TomlSerializer.Deserialize<TomlTable>(toml)
			?? throw new FormatException("Voxel palette: failed to parse TOML");

		var palette = new VoxelPaletteFile {
			MaxEmissive = GetFloat(table, "max_emissive", 1f)
		};

		if (table.TryGetValue("materials", out var slotList) && slotList is TomlTableArray slotTables)
			for (var i = 0; i < slotTables.Count && i < SlotCount; i++) {
				var source = slotTables[i];
				var slot = palette.Slots[i];
				slot.Name = GetString(source, "name", "");
				slot.PhysicsUid = GetString(source, "physics", "");
				slot.DestructionUid = GetString(source, "destruction", "");
				slot.ImpactSoundUid = GetString(source, "impact_sound", "");
				slot.Tag = GetString(source, "tag", "");

				if (source.TryGetValue("dust_colour", out var dust) && dust is TomlArray rgb && rgb.Count == 3)
					slot.DustColour = [ToByte(rgb[0]), ToByte(rgb[1]), ToByte(rgb[2])];
			}

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
			target.Alpha = GetFloat(entry, "alpha", 1f);

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
		var root = new TomlTable {
			["max_emissive"] = (double)MaxEmissive
		};

		var slots = new TomlTableArray();
		foreach (var slot in Slots) {
			var table = new TomlTable { ["name"] = slot.Name };
			if (slot.PhysicsUid.Length > 0) table["physics"] = slot.PhysicsUid;
			if (slot.DestructionUid.Length > 0) table["destruction"] = slot.DestructionUid;
			if (slot.ImpactSoundUid.Length > 0) table["impact_sound"] = slot.ImpactSoundUid;
			if (slot.Tag.Length > 0) table["tag"] = slot.Tag;
			table["dust_colour"] = new TomlArray {
				(long)slot.DustColour[0], (long)slot.DustColour[1], (long)slot.DustColour[2]
			};
			slots.Add(table);
		}

		root["materials"] = slots;

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
				["emissive"] = (double)Clamp01(entry.Emissive),
				["alpha"] = (double)Clamp01(entry.Alpha)
			};
			if (entry.HasMaterial) table["material"] = (long)Math.Clamp(entry.Material, 0, SlotCount - 1);
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
