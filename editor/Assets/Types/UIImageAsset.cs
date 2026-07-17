using System.Collections.Generic;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class UIImageAsset : BaseAsset {
	public override string Type => "ui_image";
	public override string Extension => ".tga";
	public override string DisplayName => "UI Image";
	public override string ChipText => "IMG";
	public override string ChipColor => "Blue";
	public override LucideIconKind Icon => LucideIconKind.FileImage;
	public override bool CanBeCreated => false;
	public override string Category => "";
	public override bool HasThumbnail => true;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";

	public override IReadOnlyList<string> CppTypeNames => ["UIImage"];

	public override void GenerateThumbnail() {
		if (!string.IsNullOrEmpty(Uid) && Meta?.TryGetValue("source", out var src) == true && src is string source)
			ThumbnailService.Generate(source, Uid);
	}
}
