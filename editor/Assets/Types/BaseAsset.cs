using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Lucide.Avalonia;
using Tomlyn.Model;

namespace editor.Assets.Types;

public abstract class BaseAsset {
	public abstract string Type { get; }
	public abstract string Extension { get; }
	/// <summary>Further extensions files of this type keep, e.g. a video imported as .mp4.</summary>
	public virtual IReadOnlyList<string> ExtraExtensions => [];
	public IEnumerable<string> AllExtensions => ExtraExtensions.Prepend(Extension);
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

	/// <summary>C++ class names used in Handle&lt;T&gt; in reflection data</summary>
	public virtual IReadOnlyList<string> CppTypeNames {
		get {
			var parts = Type.Split('_');
			return [string.Concat(parts.Select(p => char.ToUpperInvariant(p[0]) + p[1..]))];
		}
	}

	public virtual void GenerateThumbnail(string realPath, string uid) { }

	public virtual Task CreateAsync(string path) {
		File.WriteAllText(path, "");
		return Task.CompletedTask;
	}
}
