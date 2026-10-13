using System;
using System.Collections.Generic;
using System.IO;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets;
using editor.Assets.Types;

namespace editor.Git;

public partial class GitFileRow : ObservableObject {
	[ObservableProperty] private GitFileStatus m_status;

	public GitFileRow(string key, string repoRoot, GitFileStatus status = GitFileStatus.None) {
		Key = key;
		Status = status;
		Name = Path.GetFileName(key);
		RelPath = ProjectContext.ToVirtual(key) ?? GitParsers.Rel(repoRoot, key);

		var ext = AssetTypeRegistry.GetExtension(Name);
		var definition = AssetTypeRegistry.ByExtension(ext);
		if (definition is not null) {
			TypeLabel = definition.ChipText;
			TypeBrush = GitStatusStyle.Brush(definition.ChipColor);
		} else {
			var plain = Path.GetExtension(Name).TrimStart('.');
			TypeLabel = plain.Length == 0 ? "FILE" : plain.ToUpperInvariant();
			TypeBrush = GitStatusStyle.Brush("TextMuted");
		}
	}

	public string Key { get; }
	public string Name { get; }
	public string RelPath { get; }
	public string TypeLabel { get; }
	public IBrush TypeBrush { get; }
	public List<GitChange> Members { get; } = [];

	public static string KeyOf(string path) {
		return path.EndsWith(".meta", StringComparison.OrdinalIgnoreCase) ? path[..^".meta".Length] : path;
	}
}
