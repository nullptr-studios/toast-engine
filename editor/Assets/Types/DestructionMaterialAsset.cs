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
			$"density = 2000.0\n" +
			$"toughness = 100.0\n" +
			$"hardness = \"medium\"\n" +
			$"fracture_pattern = \"brittle\"\n" +
			$"structural_strength = 1000000.0\n" +
			$"destructible = true\n" +
			$"flammable = false\n" +
			$"burn_rate = 0.0\n" +
			$"\n[energy]\n" +
			$"debris_share = 0.2\n" +
			$"fragment_share = 0.5\n" +
			$"\n[fragment]\n" +
			$"threshold = 0.3\n" +
			$"max_parts = 4\n" +
			$"\n[shatter]\n" +
			$"threshold = 0.1\n" +
			$"cell_size = 0.5\n" +
			$"crack_cost = 1.0\n" +
			$"release = 0.3\n" +
			$"stretch = 3.0\n" +
			$"crumble = 0.5\n" +
			$"\n[edge]\n" +
			$"noise = 0.25\n" +
			$"lobes = 0.35\n" +
			$"noise_scale = 4.0\n");
		return Task.CompletedTask;
	}
}
