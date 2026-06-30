using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class ShaderAsset : BaseAsset {
	public override string Type => "shader";
	public override string Extension => ".slang";
	public override string DisplayName => "Shader";
	public override string ChipText => "SHADER";
	public override string ChipColor => "Green";
	public override LucideIconKind Icon => LucideIconKind.WandSparkles;
	public override bool CanBeCreated => true;
	public override string Category => "Visual";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";

	public override Task CreateAsync(string path) {
		File.WriteAllText(path, "// Toast Shader\n");
		return Task.CompletedTask;
	}
}
