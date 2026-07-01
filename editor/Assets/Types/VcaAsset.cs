using System.Collections.Generic;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class VcaAsset : BaseAsset {
	public override string Type => "audio_vca";
	public override IReadOnlyList<string> CppTypeNames => ["AudioVca"];
	public override string Extension => ".tvca";
	public override string DisplayName => "Audio VCA";
	public override string ChipText => "VCA";
	public override string ChipColor => "Beige";
	public override LucideIconKind Icon => LucideIconKind.SlidersHorizontal;
	public override bool CanBeCreated => false;
	public override string Category => "";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";
}
