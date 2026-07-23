using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class MaterialAsset : BaseAsset {
	public override string Type => "material";
	public override string Extension => ".tmat";
	public override string DisplayName => "Material";
	public override string ChipText => "MAT";
	public override string ChipColor => "Green";
	public override LucideIconKind Icon => LucideIconKind.Eclipse;
	public override bool CanBeCreated => true;
	public override string Category => "Visual";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "GenericEditor";

	public override string SchemaPath => "";

	public override Task CreateAsync(string path) {
		File.WriteAllText(path,
			"# Toast Material\n" +
			"name = \"Unnamed Material\"\n" +
			"shaders = []\n" +
			"\n" +
			"[settings]\n" +
			"blend_mode = \"opaque\"\n" +
			"depth_test = true\n" +
			"depth_write = true\n" +
			"cull_mode = \"back\"\n");
		return Task.CompletedTask;
	}
}
