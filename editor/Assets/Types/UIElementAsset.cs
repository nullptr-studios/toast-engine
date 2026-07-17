using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class UIElementAsset : BaseAsset {
	public override string Type => "ui_element";
	public override string Extension => ".rml";
	public override string DisplayName => "UI Element";
	public override string ChipText => "RML";
	public override string ChipColor => "Blue";
	public override LucideIconKind Icon => LucideIconKind.AppWindow;
	public override bool CanBeCreated => true;
	public override string Category => "UI";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => false;
	public override string EditorTool => "";
	public override string SchemaPath => "";

	public override IReadOnlyList<string> CppTypeNames => ["UIElement"];

	public override Task CreateAsync(string path) {
		File.WriteAllText(path,
			"""
			<rml>
			<head>
			</head>
			<body>
			</body>
			</rml>
			""");
		return Task.CompletedTask;
	}
}
