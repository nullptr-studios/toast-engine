// TODO(schema-uid): After the first asset-database rebuild, open
//   engine/assets/schemas/haptic.schema.json.meta, copy the 11-char UID, and paste it below
//   in place of the placeholder so that newly created .thaptic files embed the correct schema key.
using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class HapticAsset : BaseAsset {
	public override string Type        => "haptic";
	public override string Extension   => ".thaptic";
	public override string DisplayName => "Haptic Effect";
	public override string ChipText    => "HAPTIC";
	public override string ChipColor   => "Yellow";
	public override LucideIconKind Icon => LucideIconKind.Vibrate;
	public override bool   CanBeCreated => true;
	public override string Category    => "Input";
	public override bool   HasThumbnail => false;
	public override bool   CanBeEdited  => true;
	public override string EditorTool  => "HapticsEditor";
	public override string SchemaPath  => "core://schemas/haptic.schema.json";

	public override Task CreateAsync(string path) {
		const string schemaUid = "QGNEV1Z6qEI";
		File.WriteAllText(path,
			$"# Toast Haptic Effect\n" +
			$"schema = \"{schemaUid}\"\n" +
			$"mode = \"standard\"\n" +
			$"priority = 0\n" +
			$"duration_ms = 200\n" +
			$"left = 0.5\n" +
			$"right = 0.5\n");
		return Task.CompletedTask;
	}
}
