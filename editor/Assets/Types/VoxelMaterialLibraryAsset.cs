using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class VoxelMaterialLibraryAsset : BaseAsset {
	public override string Type => "voxel_material_library";
	public override string Extension => ".tvmat";
	public override string DisplayName => "Voxel Material Library";
	public override string ChipText => "VMAT";
	public override string ChipColor => "Orange";
	public override LucideIconKind Icon => LucideIconKind.Database;
	public override bool CanBeCreated => true;
	public override string Category => "Visual";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "GenericEditor";
	public override string SchemaPath => "";

	public override Task CreateAsync(string path) {
		// Material 0 is the fallback for every palette entry
		File.WriteAllText(path,
			"# Toast Voxel Material Library\n" +
			"[[materials]]\n" +
			"name = \"default\"\n" +
			"density = 1000\n" +
			"toughness = 100.0\n" +
			"structural_strength = 0.2\n");
		return Task.CompletedTask;
	}
}
