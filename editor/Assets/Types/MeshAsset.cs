using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class MeshAsset : BaseAsset {
	public override string Type => "mesh";
	public override string Extension => ".tmesh";
	public override string DisplayName => "Mesh";
	public override string ChipText => "MESH";
	public override string ChipColor => "Blue";
	public override LucideIconKind Icon => LucideIconKind.Shapes;
	public override bool CanBeCreated => false;
	public override string Category => "";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
