using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class InputLayoutAsset : BaseAsset {
	public override string Type        => "input_layout";
	public override string Extension   => ".tlayout";
	public override string DisplayName => "Input Layout";
	public override string ChipText    => "LAYOUT";
	public override string ChipColor   => "Yellow";
	public override LucideIconKind Icon => LucideIconKind.Gamepad2;
	public override bool   CanBeCreated => true;
	public override string Category    => "Input";
	public override bool   HasThumbnail => false;
	public override bool   CanBeEdited  => true;
	public override string EditorTool  => "GenericEditor";
	public override string SchemaPath  => "core://schemas/layout.schema.json";

	public override Task CreateAsync(string path) {
		const string schemaUid = "QcmhPPhyB1Y";
		File.WriteAllText(path,
			$"# Toast Input Layout\n" +
			$"schema = \"{schemaUid}\"\n" +
			$"name = \"Unnamed Layout\"\n");
		return Task.CompletedTask;
	}
}
