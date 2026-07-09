using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class ProjectSettingsAsset : BaseAsset {
	public override string Type => "project_settings";
	public override string Extension => ".toast";
	public override string DisplayName => "Project Settings";
	public override string ChipText => "SETTINGS";
	public override string ChipColor => "DeepPink";
	public override LucideIconKind Icon => LucideIconKind.Settings;
	public override bool CanBeCreated => false;
	public override string Category => "Project";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "GenericEditor";
	public override string SchemaPath => "core://schemas/project_settings.schema.json";
}
