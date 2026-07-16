using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using Avalonia.Media;
using editor.Assets;
using editor.Components.Elements;

namespace editor.Components.Modals;

public class AssetPickerItem {
	public AssetPickerItem(string uid, string name, string path, IBrush typeColor, string typeLabel) {
		Uid = uid;
		Name = name;
		Path = path;
		TypeColor = typeColor;
		TypeLabel = typeLabel;
		Segments.Add(new TextSegment(name, false));
	}

	public string Uid { get; }
	public string Name { get; }
	public string Path { get; }
	public IBrush TypeColor { get; }
	public string TypeLabel { get; }
	public bool IsExpanded { get; set; } = true;
	public ObservableCollection<TextSegment> Segments { get; } = [];

	public bool UpdateSegments(string query, bool caseSensitive) {
		Segments.Clear();
		if (string.IsNullOrEmpty(query)) {
			Segments.Add(new TextSegment(Name, false));
			return true;
		}

		var cmp = caseSensitive ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;
		var idx = Name.IndexOf(query, cmp);
		if (idx < 0) {
			Segments.Add(new TextSegment(Name, false));
			return false;
		}

		if (idx > 0) Segments.Add(new TextSegment(Name[..idx], false));
		Segments.Add(new TextSegment(Name.Substring(idx, query.Length), true));
		if (idx + query.Length < Name.Length) Segments.Add(new TextSegment(Name[(idx + query.Length)..], false));
		return true;
	}
}

public class AssetListPickerViewModel : PickerViewModel {
	private readonly List<AssetPickerItem> m_all = [];
	private readonly ObservableCollection<AssetPickerItem> m_filtered = [];

	public AssetListPickerViewModel(string? assetType) {
		foreach (var item in EnumerateAssets(assetType)) m_all.Add(item);
		m_all.Sort((a, b) => string.Compare(a.Name, b.Name, StringComparison.OrdinalIgnoreCase));
		foreach (var item in m_all) m_filtered.Add(item);
	}

	public override string WindowTitle => "Select an Asset...";
	public override IEnumerable Items => m_filtered;

	private static IEnumerable<AssetPickerItem> EnumerateAssets(string? typeFilter) {
		if (!ProjectContext.IsInitialized) yield break;
		foreach (var root in ProjectContext.DatabaseRoots.Append(ProjectContext.CorePath)) {
			if (!Directory.Exists(root)) continue;
			foreach (var file in Flatten(new AssetFolder(root))) {
				if (typeFilter is not null &&
				    !string.Equals(file.Definition?.Type, typeFilter, StringComparison.OrdinalIgnoreCase)) continue;
				if (file.Uid is not { } uid) continue;
				var assetReal = file.Filepath[..^5];
				var path = ProjectContext.ToVirtual(assetReal) ?? assetReal;
				yield return new AssetPickerItem(uid, file.Name, path, file.TypeColor, file.TypeLabel);
			}
		}
	}

	private static IEnumerable<AssetFile> Flatten(AssetFolder folder) {
		foreach (var file in folder.Files) yield return file;
		foreach (var sub in folder.SubFolders)
		foreach (var file in Flatten(sub))
			yield return file;
	}

	public override void UpdateFilter(string query, bool caseSensitive) {
		var visible = m_all.Where(i => i.UpdateSegments(query, caseSensitive)).ToList();
		m_filtered.Clear();
		foreach (var item in visible) m_filtered.Add(item);
	}

	public override bool IsSelectable(object? item) {
		return item is AssetPickerItem;
	}

	public override string? GetResult(object? selected) {
		return (selected as AssetPickerItem)?.Uid;
	}
}
