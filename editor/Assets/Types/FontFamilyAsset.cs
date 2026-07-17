using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class FontFamilyAsset : BaseAsset {
	public override string Type => "font_family";
	public override string Extension => ".tff";
	public override string DisplayName => "Font Family";
	public override string ChipText => "FAMILY";
	public override string ChipColor => "Blue";
	public override LucideIconKind Icon => LucideIconKind.BookType;
	public override bool CanBeCreated => true;
	public override string Category => "UI";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "GenericEditor";
	public override string SchemaPath => "core://schemas/font_family.schema.json";

	public override IReadOnlyList<string> CppTypeNames => ["FontFamily"];

	public override Task CreateAsync(string path) {
		const string schemaUid = "dyYOpW7seGM";
		File.WriteAllText(path,
			$"# Toast Font Family\n" +
			$"schema = \"{schemaUid}\"\n" +
			$"name = \"{Path.GetFileNameWithoutExtension(path)}\"\n" +
			$"fonts = []\n");
		return Task.CompletedTask;
	}
}
