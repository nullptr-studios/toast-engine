//
// VoxelPaletteFile.cs
// 12 Sep 2026
//

using System;
using System.Buffers.Binary;
using System.IO.Compression;
using System.IO;
using System.Linq;
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

	/// <summary>Mirrors voxel::k_default_ior</summary>
	public const float DefaultIor = 1.5f;
	public const float MaxIor = 3.55f;

	public float Ior { get; set; } = DefaultIor;

	public int Material { get; set; }

	/// <summary>Authored without a material which differs from material 0</summary>
	public bool HasMaterial { get; set; }

	public int TransformsTo { get; set; }
	public bool Transparent { get; set; }

	public void CopyFrom(VoxelPaletteEntry other) {
		InUse = other.InUse;
		R = other.R;
		G = other.G;
		B = other.B;
		Roughness = other.Roughness;
		Metallic = other.Metallic;
		Reflectivity = other.Reflectivity;
		Emissive = other.Emissive;
		Alpha = other.Alpha;
		Ior = other.Ior;
		Material = other.Material;
		HasMaterial = other.HasMaterial;
		TransformsTo = other.TransformsTo;
		Transparent = other.Transparent;
	}

	public void Clear() {
		InUse = false;
		HasMaterial = false;
		Material = 0;
		R = G = B = 0;
		Roughness = Metallic = Reflectivity = Emissive = 0f;
		Alpha = 1f;
		Ior = DefaultIor;
		TransformsTo = 0;
		Transparent = false;
	}
}

public sealed class VoxelMaterialSlot {
	public string Name { get; set; } = "";

	public string PhysicsUid { get; set; } = "";

	public string DestructionUid { get; set; } = "";

	public string ImpactSoundUid { get; set; } = "";
	public byte[] DustColour { get; set; } = [128, 128, 128];
	public string Tag { get; set; } = "";

	public bool IsEmpty =>
		Name.Length == 0 && PhysicsUid.Length == 0 && DestructionUid.Length == 0
		&& ImpactSoundUid.Length == 0 && Tag.Length == 0
		&& DustColour is [128, 128, 128];
}

/// <summary>Mirrors the assets::VoxelPalette TOML shape</summary>
public sealed class VoxelPaletteFile {
	public const int Size = 256;

	public const int SlotCount = 256;

	public const int RowCount = 16;

	public string[] RowLabels { get; } = Enumerable.Repeat("", RowCount).ToArray();

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

		if (table.TryGetValue("row_labels", out var labels) && labels is TomlArray labelArray)
			for (var i = 0; i < labelArray.Count && i < RowCount; i++)
				palette.RowLabels[i] = labelArray[i] as string ?? "";

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

			target.Roughness = GetFloat(entry, "roughness", 1f);
			target.Metallic = GetFloat(entry, "metallic", 0f);
			target.Reflectivity = GetFloat(entry, "reflectivity", 0f);
			target.Emissive = GetFloat(entry, "emissive", 0f);
			target.Alpha = GetFloat(entry, "alpha", 1f);
			target.Ior = GetFloat(entry, "ior", VoxelPaletteEntry.DefaultIor);

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

		if (RowLabels.Any(label => label.Length > 0)) {
			var labels = new TomlArray();
			foreach (var label in RowLabels) labels.Add(label);
			root["row_labels"] = labels;
		}

		// Trailing slots nobody authored are left out
		var slotCount = Slots.Length;
		while (slotCount > 0 && Slots[slotCount - 1].IsEmpty) slotCount--;

		var slots = new TomlTableArray();
		foreach (var slot in Slots.Take(slotCount)) {
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
			if (Math.Abs(entry.Ior - VoxelPaletteEntry.DefaultIor) > 0.005f)
				table["ior"] = Math.Round((double)Math.Clamp(entry.Ior, 1f, VoxelPaletteEntry.MaxIor), 2);
			if (entry.HasMaterial) table["material"] = (long)Math.Clamp(entry.Material, 0, SlotCount - 1);
			table["transforms_to"] = (long)Math.Clamp(entry.TransformsTo, 0, 255);
			table["transparent"] = entry.Transparent;
			list.Add(table);
		}

		root["entries"] = list;
		return TomlSerializer.Serialize(root);
	}

	public byte[] ToPng() {
		var row = new byte[1 + Size * 4];    // filter byte then pixels
		for (var i = 1; i < Size; i++) {
			var entry = Entries[i];
			var o = 1 + i * 4;
			if (i == Size - 1) {
				row[o] = row[o + 1] = row[o + 2] = row[o + 3] = 255;
			} else if (entry.InUse) {
				row[o] = entry.R;
				row[o + 1] = entry.G;
				row[o + 2] = entry.B;
				row[o + 3] = entry.Transparent ? (byte)Math.Round(Clamp01(entry.Alpha) * 255f) : (byte)255;
			}
		}

		using var output = new MemoryStream();
		output.Write([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);

		var header = new byte[13];
		BinaryPrimitives.WriteInt32BigEndian(header, Size);
		BinaryPrimitives.WriteInt32BigEndian(header.AsSpan(4), 1);
		header[8] = 8;    // bit depth
		header[9] = 6;    // RGBA
		WriteChunk(output, "IHDR", header);

		using var compressed = new MemoryStream();
		using (var z = new ZLibStream(compressed, CompressionLevel.Optimal, true)) z.Write(row);
		WriteChunk(output, "IDAT", compressed.ToArray());
		WriteChunk(output, "IEND", []);
		return output.ToArray();
	}

	private static void WriteChunk(Stream output, string type, byte[] data) {
		var word = new byte[4];
		BinaryPrimitives.WriteInt32BigEndian(word, data.Length);
		output.Write(word);

		var body = new byte[4 + data.Length];
		System.Text.Encoding.ASCII.GetBytes(type, body);
		data.CopyTo(body, 4);
		output.Write(body);

		var crc = 0xFFFFFFFFu;
		foreach (var b in body) {
			crc ^= b;
			for (var k = 0; k < 8; k++) crc = (crc & 1) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
		}

		BinaryPrimitives.WriteUInt32BigEndian(word, ~crc);
		output.Write(word);
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
