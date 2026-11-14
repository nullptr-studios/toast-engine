using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class SkeletonAsset : BaseAsset {
	public override string Type => "skeleton";
	public override string Extension => ".tskel";
	public override string DisplayName => "Skeleton";
	public override string ChipText => "SKEL";
	public override string ChipColor => "Purple";
	public override LucideIconKind Icon => LucideIconKind.Bone;
	public override bool CanBeCreated => false;
	public override string Category => "Visual";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
