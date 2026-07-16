using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class AudioEventAsset : BaseAsset {
	public override string Type => "audio_event";
	public override string Extension => ".tae";
	public override string DisplayName => "Audio Event";
	public override string ChipText => "EVENT";
	public override string ChipColor => "Beige";
	public override LucideIconKind Icon => LucideIconKind.Volume2;
	public override bool CanBeCreated => false;
	public override string Category => "";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
