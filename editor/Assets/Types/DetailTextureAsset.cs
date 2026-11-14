using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class DetailTextureAsset : BaseAsset {
	public override string Type => "detail_texture";
	public override string Extension => ".tdet";
	public override string DisplayName => "Detail Texture";
	public override string ChipText => "DET";
	public override string ChipColor => "Orange";
	public override LucideIconKind Icon => LucideIconKind.Layers;
	public override bool CanBeCreated => true;
	public override string Category => "Visual";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;

	public override string EditorTool => "DetailTextureEditor";

	public override string SchemaPath => "";

	public override Task CreateAsync(string path) {
		File.WriteAllText(path, "# Toast Detail Texture\n");
		return Task.CompletedTask;
	}
}
