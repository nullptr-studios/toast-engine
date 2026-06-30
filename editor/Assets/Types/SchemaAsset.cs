using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class SchemaAsset : BaseAsset {
	public override string Type => "schema";
	public override string Extension => ".schema.json";
	public override string DisplayName => "Schema";
	public override string ChipText => "SCHEMA";
	public override string ChipColor => "Cyan";
	public override LucideIconKind Icon => LucideIconKind.Settings;
	public override bool CanBeCreated => true;
	public override string Category => "Data";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "SchemaEditor";
	public override string SchemaPath => "";

	public override Task CreateAsync(string path) {
		File.WriteAllText(path, "{}\n");
		return Task.CompletedTask;
	}
}
