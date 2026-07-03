using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class AudioBusAsset : BaseAsset {
	public override string Type => "audio_bus";
	public override string Extension => ".tbus";
	public override string DisplayName => "Audio Bus";
	public override string ChipText => "BUS";
	public override string ChipColor => "Beige";
	public override LucideIconKind Icon => LucideIconKind.SlidersVertical;
	public override bool CanBeCreated => false;
	public override string Category => "";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
