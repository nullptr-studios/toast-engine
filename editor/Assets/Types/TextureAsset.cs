using System;
using System.IO;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class TextureAsset : BaseAsset {
	public override string Type => "texture";
	public override string Extension => ".ktx2";
	public override string DisplayName => "Texture";
	public override string ChipText => "TEX";
	public override string ChipColor => "Orange";
	public override LucideIconKind Icon => LucideIconKind.Image;
	public override bool CanBeCreated => false;
	public override string Category => "Visual";
	public override bool HasThumbnail => true;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";

	public override void GenerateThumbnail(string realPath, string uid) {
		var source = MetaFile.ReadHeader(realPath)?.Source;
		var realSource = string.IsNullOrEmpty(source) ? null : ProjectContext.Resolve(source);
		if (realSource is null || !File.Exists(realSource))
			throw new Exception($"cannot regenerate thumbnail for '{realPath}': its original source is missing");

		ThumbnailService.Generate(realSource, uid);
	}
}
