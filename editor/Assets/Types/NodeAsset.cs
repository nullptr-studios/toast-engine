using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class NodeAsset : BaseAsset {
	public override string Type => "node";
	public override string Extension => ".tnode";
	public override string DisplayName => "Node";
	public override string ChipText => "NODE";
	public override string ChipColor => "Red";
	public override LucideIconKind Icon => LucideIconKind.Box;
	public override bool CanBeCreated => false;
	public override string Category => "";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "NodeEditor";
	public override string SchemaPath => "";
}
