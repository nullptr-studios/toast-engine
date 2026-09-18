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
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
