using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public class PostProcessProfileAsset : BaseAsset {
	public override string Type => "post_process_profile";
	public override string Extension => ".tpp";
	public override string DisplayName => "Post Process Profile";
	public override string ChipText => "POST";
	public override string ChipColor => "Orange";
	public override LucideIconKind Icon => LucideIconKind.Sun;
	public override bool CanBeCreated => true;
	public override string Category => "Visual";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "GenericEditor";
	public override string SchemaPath => "core://schemas/post_process.schema.json";

	public override IReadOnlyList<string> CppTypeNames => ["PostProcessProfile"];

	public override Task CreateAsync(string path) {
		const string schemaUid = "PostProcSc4";
				File.WriteAllText(path,
        			$"# Toast post process profile\n" +
        			$"schema = \"{schemaUid}\"\n");
		return Task.CompletedTask;
	}
}
