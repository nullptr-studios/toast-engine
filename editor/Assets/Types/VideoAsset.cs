using System.Collections.Generic;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class VideoAsset : BaseAsset {
	public override string Type => "video";
	public override string Extension => ".webm";
	public override IReadOnlyList<string> ExtraExtensions => [".mp4", ".mkv"];
	public override string DisplayName => "Video";
	public override string ChipText => "VID";
	public override string ChipColor => "Orange";
	public override LucideIconKind Icon => LucideIconKind.Film;
	public override bool CanBeCreated => false;
	public override string Category => "Visual";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
