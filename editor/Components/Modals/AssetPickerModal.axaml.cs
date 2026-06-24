using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using editor.Assets;
using editor.Components.Elements;

namespace editor.Components.Modals;

public class AssetPickerItem {
	public AssetPickerItem(string uid, string name, string path, IBrush typeColor) {
		Uid = uid;
		Name = name;
		Path = path;
		TypeColor = typeColor;
		Segments.Add(new TextSegment(name, false));
	}

	public string Uid { get; }
	public string Name { get; }
	public string Path { get; }
	public IBrush TypeColor { get; }
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

public partial class AssetPickerModal : Window {
	private readonly List<AssetPickerItem> m_all = [];
	private readonly ObservableCollection<AssetPickerItem> m_filtered = [];
	private AssetPickerItem? m_selected;

	public AssetPickerModal() {
		InitializeComponent();
		AssetList.ItemsSource = m_filtered;
	}

	public AssetPickerModal(string? assetType) : this() {
		FileType? filter = Enum.TryParse<FileType>(assetType, true, out var ft) ? ft : null;
		foreach (var item in EnumerateAssets(filter)) m_all.Add(item);
		m_all.Sort((a, b) => string.Compare(a.Name, b.Name, StringComparison.OrdinalIgnoreCase));
		foreach (var item in m_all) m_filtered.Add(item);
	}

	private static IEnumerable<AssetPickerItem> EnumerateAssets(FileType? filter) {
		if (!ProjectContext.IsInitialized) yield break;

		foreach (var root in new[] { ProjectContext.AssetsPath, ProjectContext.CorePath }) {
			if (!Directory.Exists(root)) continue;
			foreach (var file in Flatten(new AssetFolder(root))) {
				if (filter is { } f && file.Type != f) continue;
				if (file.Uid is not { } uid) continue;

				var assetReal = file.Filepath[..^5]; // strip ".meta"
				var path = ProjectContext.ToVirtual(assetReal) ?? assetReal;
				yield return new AssetPickerItem(uid, file.Name, path, file.TypeColor);
			}
		}
	}

	private static IEnumerable<AssetFile> Flatten(AssetFolder folder) {
		foreach (var file in folder.Files) yield return file;
		foreach (var sub in folder.SubFolders)
		foreach (var file in Flatten(sub))
			yield return file;
	}

	private void OnSearchChanged(object? sender, TextChangedEventArgs e) {
		var query = SearchBox.Text ?? "";
		var caseSensitive = query.Any(char.IsUpper);

		var visible = m_all.Where(i => i.UpdateSegments(query, caseSensitive)).ToList();
		m_filtered.Clear();
		foreach (var item in visible) m_filtered.Add(item);
	}

	private void OnSelectionChanged(object? sender, SelectionChangedEventArgs e) {
		m_selected = AssetList.SelectedItem as AssetPickerItem;
	}

	private void OnItemDoubleTapped(object? sender, TappedEventArgs e) {
		if (m_selected is not null) Close(m_selected.Uid);
	}

	private void OnSelect(object? sender, RoutedEventArgs e) {
		if (m_selected is not null) Close(m_selected.Uid);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}
}
