using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class AnimationAsset : BaseAsset {
	public override string Type => "animation";
	public override string Extension => ".tanim";
	public override string DisplayName => "Animation";
	public override string ChipText => "ANIM";
	public override string ChipColor => "Purple";
	public override LucideIconKind Icon => LucideIconKind.Film;
	public override bool CanBeCreated => false;
	public override string Category => "";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
