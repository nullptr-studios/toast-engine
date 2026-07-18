using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using editor.Assets;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class LocalizationAsset : BaseAsset {
	public override string Type => "localization";
	public override string Extension => ".tloc";
	public override string DisplayName => "Localization";
	public override string ChipText => "LOC";
	public override string ChipColor => "Blue";
	public override LucideIconKind Icon => LucideIconKind.Languages;
	public override bool CanBeCreated => true;
	public override string Category => "UI";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "TableEditor";
	public override string SchemaPath => "";

	public override IReadOnlyList<string> CppTypeNames => ["Localization"];

	public override Task CreateAsync(string path) {
		var header = new List<string> { "id" };
		header.AddRange(ProjectContext.Languages);
		File.WriteAllText(path, string.Join(",", header) + "\n");
		return Task.CompletedTask;
	}
}
