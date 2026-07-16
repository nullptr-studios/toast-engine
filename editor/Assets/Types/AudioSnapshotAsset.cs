using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class AudioSnapshot : BaseAsset {
	public override string Type => "audio_snapshot";
	public override string Extension => ".tasnap";
	public override string DisplayName => "Audio Snapshot";
	public override string ChipText => "SNAP";
	public override string ChipColor => "Beige";
	public override LucideIconKind Icon => LucideIconKind.Activity;
	public override bool CanBeCreated => false;
	public override string Category => "";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
