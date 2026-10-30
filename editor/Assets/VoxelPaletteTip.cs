//
// VoxelPaletteTip.cs by Xein
// 12 Oct 2026
//

using System;
using System.Globalization;
using Avalonia.Media;

namespace editor.Assets;

public sealed record VoxelPaletteTip(
	string Title,
	bool InUse,
	Color Albedo,
	string AlbedoText,
	Color Rms,
	string RmsText,
	string EmissiveText,
	string AlphaText
) {
	public static int SlotOf(VoxelPaletteEntry entry) {
		return entry.HasMaterial ? Math.Clamp(entry.Material, 0, VoxelPaletteFile.SlotCount - 1) : 0;
	}

	public static string SlotName(int index, VoxelMaterialSlot slot) {
		return index == 0 ? "Default"
			: string.IsNullOrWhiteSpace(slot.Name) ? $"Slot {index}"
			: slot.Name;
	}

	public static string Hex(int id) {
		return $"{id - 1:X2}";
	}

	public static string Float(float value) {
		return value.ToString("0.0#", CultureInfo.InvariantCulture);
	}

	public static VoxelPaletteTip Describe(int id, VoxelPaletteFile palette) {
		var entry = palette.Entries[id];
		if (!entry.InUse)
			return new VoxelPaletteTip($"{id}: empty ({Hex(id)})", false, default, "", default, "", "", "");

		var slot = SlotOf(entry);
		var maxEmissive = palette.MaxEmissive;

		return new VoxelPaletteTip(
			$"{id}: {SlotName(slot, palette.Slots[slot])} ({Hex(id)})",
			true,
			Color.FromRgb(entry.R, entry.G, entry.B),
			$"{Float(entry.R / 255f)} {Float(entry.G / 255f)} {Float(entry.B / 255f)} rgb",
			Color.FromRgb(ToByte(entry.Roughness), ToByte(entry.Metallic), ToByte(entry.Reflectivity)),
			$"{Float(entry.Roughness)} {Float(entry.Metallic)} {Float(entry.Reflectivity)} rms", /// rms dot
			$"{Float(entry.Emissive * maxEmissive)} emissive ({Float(maxEmissive)} max)",
			$"{Float(entry.Alpha)} {(entry.Transparent ? "transparent" : "opaque")}"
		);
	}

	private static byte ToByte(float unit) {
		return (byte)Math.Clamp(MathF.Round(unit * 255f), 0f, 255f);
	}
}
