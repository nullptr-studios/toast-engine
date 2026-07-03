using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class AudioBankAsset : BaseAsset {
	public override string Type => "audio_bank";
	public override string Extension => ".bank";
	public override string DisplayName => "Audio Bank";
	public override string ChipText => "BANK";
	public override string ChipColor => "Beige";
	public override LucideIconKind Icon => LucideIconKind.AudioWaveform;
	public override bool CanBeCreated => false;
	public override string Category => "";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
