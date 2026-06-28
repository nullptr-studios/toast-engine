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
	public override string Category => "";
	public override bool HasThumbnail => true;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";

	public override void GenerateThumbnail() {
		if (!string.IsNullOrEmpty(Uid) && Meta?.TryGetValue("source", out var src) == true && src is string source)
			ThumbnailService.Generate(source, Uid);
	}
}
