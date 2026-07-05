using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class InputActionAsset : BaseAsset {
	public override string Type => "input_action";
	public override string Extension => ".taction";
	public override string DisplayName => "Input Action";
	public override string ChipText => "ACTION";
	public override string ChipColor => "Yellow";
	public override LucideIconKind Icon => LucideIconKind.Zap;
	public override bool CanBeCreated => true;
	public override string Category => "Input";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "GenericEditor";
	public override string SchemaPath => "core://schemas/action.schema.json";

	public override IReadOnlyList<string> CppTypeNames => ["Action"];

	public override Task CreateAsync(string path) {
		const string schemaUid = "FlSzpXtC0P4";
		File.WriteAllText(path,
			$"# Toast Input Action\n" +
			$"schema = \"{schemaUid}\"\n" +
			$"name = \"Unnamed\"\n" +
			$"function_name = \"onUnnamed\"\n" +
			$"type = \"Action0D\"\n" +
			$"accumulation = false\n");
		return Task.CompletedTask;
	}
}
