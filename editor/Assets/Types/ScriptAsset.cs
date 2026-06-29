using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class ScriptAsset : BaseAsset {
	public override string Type => "script";
	public override string Extension => ".lua";
	public override string DisplayName => "Script";
	public override string ChipText => "LUA";
	public override string ChipColor => "Magenta";
	public override LucideIconKind Icon => LucideIconKind.CodeXml;
	public override bool CanBeCreated => true;
	public override string Category => "Logic";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";

	public override Task CreateAsync(string path) {
		File.WriteAllText(path, "-- Toast Script\n");
		return Task.CompletedTask;
	}
}
