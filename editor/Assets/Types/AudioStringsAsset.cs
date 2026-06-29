using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class AudioStringsAsset : BaseAsset {
	public override string Type => "audio_strings";
	public override string Extension => ".strings.bank";
	public override string DisplayName => "Audio Strings";
	public override string ChipText => "BSTR";
	public override string ChipColor => "Beige";
	public override LucideIconKind Icon => LucideIconKind.BookHeadphones;
	public override bool CanBeCreated => false;
	public override string Category => "";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
