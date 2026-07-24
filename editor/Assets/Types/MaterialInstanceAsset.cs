using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class MaterialInstanceAsset : BaseAsset {
	public override string Type => "material_instance";
	public override string Extension => ".tmi";
	public override string DisplayName => "Material Instance";
	public override string ChipText => "INST";
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
			"# Toast Material Instance\n" +
			"material = \"\"\n");
		return Task.CompletedTask;
	}
}
