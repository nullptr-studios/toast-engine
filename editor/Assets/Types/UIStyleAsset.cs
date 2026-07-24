using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class UIStyleAsset : BaseAsset {
	public override string Type => "ui_style";
	public override string Extension => ".rcss";
	public override string DisplayName => "UI Style";
	public override string ChipText => "STYLE";
	public override string ChipColor => "Blue";
	public override LucideIconKind Icon => LucideIconKind.Paintbrush;
	public override bool CanBeCreated => true;
	public override string Category => "UI";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";

	public override IReadOnlyList<string> CppTypeNames => ["UIStyle"];

	public override Task CreateAsync(string path) {
		File.WriteAllText(path, "body {\n}\n");
		return Task.CompletedTask;
	}
}
