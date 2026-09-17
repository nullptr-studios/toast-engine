using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public class PhysicsMaterialAsset : BaseAsset {
	public override string Type => "physics_material";
	public override string Extension => ".tpm";
	public override string DisplayName => "Physics Material";
	public override string ChipText => "PHYS";
	public override string ChipColor => "Green";
	public override LucideIconKind Icon => LucideIconKind.Atom;
	public override bool CanBeCreated => true;
	public override string Category => "Data";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "GenericEditor";
	public override string SchemaPath => "core://schemas/physics_material.schema.json";

	public override IReadOnlyList<string> CppTypeNames => ["PhysicsMaterial"];

	public override Task CreateAsync(string path) {
		const string schemaUid = "Wausy1NQVE0";
		File.WriteAllText(path,
			$"# Toast Physics Material\n" +
			$"schema = \"{schemaUid}\"\n" +
			$"restitution = 0.5\n" +
			$"static_friction = 0.6\n" +
			$"dynamic_friction = 0.4\n");
		return Task.CompletedTask;
	}
}
