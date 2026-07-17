using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class FontAsset : BaseAsset {
	public override string Type => "font";
	public override string Extension => ".ttf";
	public override string DisplayName => "Font";
	public override string ChipText => "FONT";
	public override string ChipColor => "Blue";
	public override LucideIconKind Icon => LucideIconKind.Type;
	public override bool CanBeCreated => false;
	public override string Category => "";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
