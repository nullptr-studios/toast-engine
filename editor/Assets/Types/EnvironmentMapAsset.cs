using System.Collections.Generic;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class EnvironmentMapAsset : BaseAsset {
	public override string Type => "environment_map";
	public override IReadOnlyList<string> CppTypeNames => ["EnvironmentMap"];
	public override string Extension => ".hdr";
	public override string DisplayName => "Environment Map";
	public override string ChipText => "HDRI";
	public override string ChipColor => "Orange";
	public override LucideIconKind Icon => LucideIconKind.Sun;
	public override bool CanBeCreated => false;
	public override string Category => "Visual";
	public override bool HasThumbnail => true;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";

	public override void GenerateThumbnail(string realPath, string uid) {
		ThumbnailService.GenerateFromHdr(realPath, uid);
	}
}
