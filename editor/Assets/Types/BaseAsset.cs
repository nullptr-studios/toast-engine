using System.IO;
using System.Threading.Tasks;
using Lucide.Avalonia;
using Tomlyn.Model;

namespace editor.Assets.Types;

public abstract class BaseAsset {
	public abstract string Type { get; }
	public abstract string Extension { get; }
	public abstract string DisplayName { get; }
	public abstract string ChipText { get; }
	public abstract string ChipColor { get; }
	public abstract LucideIconKind Icon { get; }
	public abstract bool CanBeCreated { get; }
	public abstract string Category { get; }
	public abstract bool HasThumbnail { get; }
	public abstract bool CanBeEdited { get; }
	public abstract string EditorTool { get; }
	public abstract string SchemaPath { get; }

	public string Uid { get; set; } = "";
	public TomlTable? Meta { get; set; }

	public virtual void GenerateThumbnail() { }

	public virtual Task CreateAsync(string path) {
		File.WriteAllText(path, "");
		return Task.CompletedTask;
	}
}
