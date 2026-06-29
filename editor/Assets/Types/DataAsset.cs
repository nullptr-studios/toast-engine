using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class DataAsset : BaseAsset {
	public override string Type => "data";
	public override string Extension => ".toml";
	public override string DisplayName => "Data";
	public override string ChipText => "DATA";
	public override string ChipColor => "Cyan";
	public override LucideIconKind Icon => LucideIconKind.Database;
	public override bool CanBeCreated => true;
	public override string Category => "Data";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "GenericEditor";
	public override string SchemaPath => "";

	public override Task CreateAsync(string path) {
		File.WriteAllText(path, "# Toast Data\n");
		return Task.CompletedTask;
	}
}
