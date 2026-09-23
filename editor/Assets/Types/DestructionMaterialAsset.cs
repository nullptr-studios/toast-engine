using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public class DestructionMaterialAsset : BaseAsset {
	public override string Type => "destruction_material";
	public override string Extension => ".tdm";
	public override string DisplayName => "Destruction Material";
	public override string ChipText => "DEST";
	public override string ChipColor => "Orange";
	public override LucideIconKind Icon => LucideIconKind.Hammer;
	public override bool CanBeCreated => true;
	public override string Category => "Physics";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "GenericEditor";
	public override string SchemaPath => "core://schemas/destruction_material.schema.json";

	public override IReadOnlyList<string> CppTypeNames => ["DestructionMaterial"];

	public override Task CreateAsync(string path) {
		const string schemaUid = "DmT4xKq9Rb0";
		File.WriteAllText(path,
			$"# Toast Destruction Material\n" +
			$"schema = \"{schemaUid}\"\n" +
			$"density = 1000.0\n" +
			$"toughness = 1000.0\n" +
			$"structural_strength = 1000.0\n" +
			$"shatter_radius = 0.5\n" +
			$"flammable = false\n" +
			$"burn_rate = 0.0\n");
		return Task.CompletedTask;
	}
}
