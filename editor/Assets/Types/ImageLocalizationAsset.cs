using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using editor.Assets;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class ImageLocalizationAsset : BaseAsset {
	public override string Type => "image_localization";
	public override string Extension => ".tiloc";
	public override string DisplayName => "Image Localization";
	public override string ChipText => "ILOC";
	public override string ChipColor => "Cyan";
	public override LucideIconKind Icon => LucideIconKind.Images;
	public override bool CanBeCreated => true;
	public override string Category => "UI";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "TableEditor";
	public override string SchemaPath => "";

	public override IReadOnlyList<string> CppTypeNames => ["ImageLocalization"];

	public override Task CreateAsync(string path) {
		var header = new List<string> { "id" };
		header.AddRange(ProjectContext.Languages);
		File.WriteAllText(path, string.Join(",", header) + "\n");
		return Task.CompletedTask;
	}
}
