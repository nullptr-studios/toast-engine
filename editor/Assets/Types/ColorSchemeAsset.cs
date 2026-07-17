using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class ColorSchemeAsset : BaseAsset {
	public override string Type => "color_scheme";
	public override string Extension => ".tcolor";
	public override string DisplayName => "Color Scheme";
	public override string ChipText => "COLOR";
	public override string ChipColor => "Blue";
	public override LucideIconKind Icon => LucideIconKind.SwatchBook;
	public override bool CanBeCreated => true;
	public override string Category => "UI";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "GenericEditor";
	public override string SchemaPath => "core://schemas/color_scheme.schema.json";

	public override IReadOnlyList<string> CppTypeNames => ["ColorScheme"];

	public override Task CreateAsync(string path) {
		const string schemaUid = "InqVb3Ti7Sc";
		File.WriteAllText(path,
			$"# Toast Color Scheme\n" +
			$"schema = \"{schemaUid}\"\n" +
			$"\n" +
			$"[[colors]]\n" +
			$"name = \"primary\"\n" +
			$"color = [1.0, 1.0, 1.0, 1.0]\n");
		return Task.CompletedTask;
	}
}
