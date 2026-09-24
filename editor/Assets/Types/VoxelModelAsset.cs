using System;
using System.IO;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class VoxelModelAsset : BaseAsset {
	public override string Type => "voxel_model";
	public override string Extension => ".tvox";
	public override string DisplayName => "Voxel Model";
	public override string ChipText => "VOX";
	public override string ChipColor => "Cyan";
	public override LucideIconKind Icon => LucideIconKind.Box;

	public override bool CanBeCreated => false;
	public override string Category => "Visual";
	public override bool HasThumbnail => true;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";

	public override void GenerateThumbnail(string realPath, string uid) {
		ThumbnailService.GenerateFromVoxel(realPath, ResolvePalettePath(realPath), uid);
	}

	private static string ResolvePalettePath(string tvoxPath) {
		try {
			Span<byte> header = stackalloc byte[24];
			using var file = File.OpenRead(tvoxPath);
			if (file.Read(header) != header.Length) return "";

			var paletteUidValue = BitConverter.ToUInt64(header[16..24]);
			if (paletteUidValue == 0) return "";

			var paletteUid = UidGenerator.EncodeNative(paletteUidValue);
			return AssetDatabase.TryResolve(paletteUid, out var virtualPath, out _) ? ProjectContext.Resolve(virtualPath) : "";
		} catch {
			return "";
		}
	}
}
