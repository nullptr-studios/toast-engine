using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class AudioPortAsset : BaseAsset {
	public override string Type => "audio_port";
	public override string Extension => ".taport";
	public override string DisplayName => "Audio Port";
	public override string ChipText => "PORT";
	public override string ChipColor => "Beige";
	public override LucideIconKind Icon => LucideIconKind.Volume2;
	public override bool CanBeCreated => false;
	public override string Category => "";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
