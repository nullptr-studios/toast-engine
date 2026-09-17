using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class VoxelPaletteAsset : BaseAsset {
	public override string Type => "voxel_palette";
	public override string Extension => ".tpal";
	public override string DisplayName => "Voxel Palette";
	public override string ChipText => "PAL";
	public override string ChipColor => "Magenta";
	public override LucideIconKind Icon => LucideIconKind.Brush;
	public override bool CanBeCreated => true;
	public override string Category => "Visual";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;

	public override string EditorTool => "PaletteEditor";

	public override string SchemaPath => "";

	public override Task CreateAsync(string path) {
		// Index 0 is the empty voxel
		File.WriteAllText(path,
			"# Toast Voxel Palette\n" +
			"max_emissive = 1.0\n" +
			"\n" +
			"[[entries]]\n" +
			"index = 1\n" +
			"albedo = [200, 200, 200]\n" +
			"roughness = 0.8\n");
		return Task.CompletedTask;
	}
}
