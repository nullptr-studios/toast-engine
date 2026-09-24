//
// AssetBrowserSettings.cs
// 24 Sep 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text.RegularExpressions;
using Avalonia.Media;
using Avalonia.Threading;
using Tomlyn;
using Tomlyn.Model;

namespace editor.Assets;

public enum AssetCardSize { Small, Medium, Large }

public enum AssetSortBy { Name, Type, Modified }

public enum AssetSearchScope { Everywhere, CurrentFolder }

public sealed class AssetTag : INotifyPropertyChanged {
	private Color m_color;
	private int m_assetCount;
	private string m_name;

	public AssetTag(string id, string name, Color color) {
		Id = id;
		m_name = name;
		m_color = color;
		Brush = new SolidColorBrush(color);
	}

	public string Id { get; }

	public string Name {
		get => m_name;
		set {
			if (m_name == value) return;
			m_name = value;
			Notify();
			AssetBrowserSettings.ScheduleSave();
		}
	}

	public Color Color {
		get => m_color;
		set {
			if (m_color == value) return;
			m_color = value;
			Brush = new SolidColorBrush(value);
			Notify();
			Notify(nameof(Brush));
			AssetBrowserSettings.ScheduleSave();
		}
	}

	public IBrush Brush { get; private set; }

	public int AssetCount {
		get => m_assetCount;
		set {
			if (m_assetCount == value) return;
			m_assetCount = value;
			Notify();
			Notify(nameof(AssetCountText));
		}
	}

	public string AssetCountText => AssetCount == 1 ? "1 asset" : $"{AssetCount} assets";

	public event PropertyChangedEventHandler? PropertyChanged;

	internal void SetSilently(string name, Color color) {
		if (m_name != name) {
			m_name = name;
			Notify(nameof(Name));
		}

		if (m_color != color) {
			m_color = color;
			Brush = new SolidColorBrush(color);
			Notify(nameof(Color));
			Notify(nameof(Brush));
		}
	}

	private void Notify([CallerMemberName] string? name = null) {
		PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
	}
}

public static class AssetBrowserSettings {
	private const string EditorKey = "editor";
	private const string SectionKey = "asset_browser";

	private static DispatcherTimer? s_saveTimer;
	private static List<Regex> s_hiddenRegexes = [];

	public static ObservableCollection<AssetTag> Tags { get; } = [];

	public static bool ShowTags { get; private set; } = true;
	public static bool ShowTypeBadge { get; private set; } = true;
	public static AssetCardSize CardSize { get; private set; } = AssetCardSize.Medium;
	public static bool ShowCore { get; private set; } = true;
	public static bool ShowCache { get; private set; }
	public static AssetSortBy SortBy { get; private set; } = AssetSortBy.Name;
	public static bool FoldersFirst { get; private set; } = true;
	public static AssetSearchScope SearchScope { get; private set; } = AssetSearchScope.Everywhere;
	public static bool ConfirmDelete { get; private set; } = true;
	public static IReadOnlyList<string> HiddenPatterns { get; private set; } = [];

	public static event Action? Changed;
	public static event Action<bool>? Saved;

	public static void Reset() {
		s_saveTimer?.Stop();
		Tags.Clear();
		ApplyDefaults();
		Changed?.Invoke();
	}

	public static void Load() {
		if (s_saveTimer?.IsEnabled == true) Save();

		ApplyDefaults();
		var loaded = new List<(string Id, string Name, Color Color)>();

		if (FindProjectFile() is { } path)
			try {
				var root = TomlSerializer.Deserialize<TomlTable>(File.ReadAllText(path));
				if (root is not null && root.TryGetValue(EditorKey, out var e) && e is TomlTable editor &&
				    editor.TryGetValue(SectionKey, out var s) && s is TomlTable section) {
					ShowTags = GetBool(section, "show_tags", true);
					ShowTypeBadge = GetBool(section, "show_type_badge", true);
					CardSize = GetEnum(section, "card_size", AssetCardSize.Medium);
					ShowCore = GetBool(section, "show_core", true);
					ShowCache = GetBool(section, "show_cache", false);
					SortBy = GetEnum(section, "sort_by", AssetSortBy.Name);
					FoldersFirst = GetBool(section, "folders_first", true);
					SearchScope = GetEnum(section, "search_scope", AssetSearchScope.Everywhere);
					ConfirmDelete = GetBool(section, "confirm_delete", true);
					if (section.TryGetValue("hidden_patterns", out var hp) && hp is TomlArray patterns)
						HiddenPatterns = patterns.Select(p => p?.ToString()?.Trim() ?? "")
							.Where(p => p.Length > 0).ToList();

					if (section.TryGetValue("tags", out var t) && t is TomlTableArray tags)
						foreach (var tag in tags) {
							var id = tag.TryGetValue("id", out var i) ? i?.ToString() : null;
							if (string.IsNullOrWhiteSpace(id) || loaded.Any(l => l.Id == id)) continue;
							var name = tag.TryGetValue("name", out var n) ? n?.ToString() ?? "" : "";
							var color = tag.TryGetValue("color", out var c) && Color.TryParse(c?.ToString(), out var parsed)
								? parsed
								: Colors.Gray;
							loaded.Add((id, name, color));
						}
				}
			} catch {
				//...
			}

		for (var i = Tags.Count - 1; i >= 0; i--)
			if (loaded.All(l => l.Id != Tags[i].Id))
				Tags.RemoveAt(i);

		for (var i = 0; i < loaded.Count; i++) {
			var (id, name, color) = loaded[i];
			var existing = Tags.FirstOrDefault(tag => tag.Id == id);
			if (existing is null) {
				Tags.Insert(i, new AssetTag(id, name, color));
				continue;
			}

			existing.SetSilently(name, color);
			var from = Tags.IndexOf(existing);
			if (from != i) Tags.Move(from, i);
		}

		RebuildHiddenRegexes();
		Changed?.Invoke();
	}

	public static AssetTag? ById(string id) {
		return Tags.FirstOrDefault(t => t.Id == id);
	}

	public static AssetTag? ByName(string name) {
		return Tags.FirstOrDefault(t => string.Equals(t.Name, name, StringComparison.OrdinalIgnoreCase));
	}

	public static IReadOnlyList<AssetTag> Resolve(IReadOnlyCollection<string> ids) {
		if (ids.Count == 0 || Tags.Count == 0) return [];
		return Tags.Where(t => ids.Contains(t.Id)).ToList();
	}

	public static AssetTag AddTag(string name, Color color) {
		var tag = new AssetTag(UidGenerator.Generate(), name, color);
		Tags.Add(tag);
		Commit();
		return tag;
	}

	public static void RemoveTag(AssetTag tag) {
		if (!Tags.Remove(tag)) return;
		Commit();
	}

	public static List<string> FindTaggedMetas(string tagId) {
		var result = new List<string>();
		if (!ProjectContext.IsInitialized) return result;
		foreach (var root in ProjectContext.DatabaseRoots) {
			if (!Directory.Exists(root)) continue;
			foreach (var meta in MetaFile.FindAll(root))
				if (MetaFile.ReadHeader(meta)?.Tags?.Contains(tagId) == true)
					result.Add(meta);
		}

		return result;
	}

	public static void DeleteTag(AssetTag tag) {
		foreach (var meta in FindTaggedMetas(tag.Id)) {
			var remaining = MetaFile.ReadHeader(meta)?.Tags?.Where(id => id != tag.Id).ToList() ?? [];
			MetaFile.SetTags(meta, remaining);
		}

		RemoveTag(tag);
	}

	public static void MoveTag(AssetTag tag, int delta) {
		var from = Tags.IndexOf(tag);
		var to = from + delta;
		if (from < 0 || to < 0 || to >= Tags.Count) return;
		Tags.Move(from, to);
		Commit();
	}

	public static void SetShowTags(bool value) => Set(ShowTags, value, v => ShowTags = v);
	public static void SetShowTypeBadge(bool value) => Set(ShowTypeBadge, value, v => ShowTypeBadge = v);
	public static void SetCardSize(AssetCardSize value) => Set(CardSize, value, v => CardSize = v);
	public static void SetShowCore(bool value) => Set(ShowCore, value, v => ShowCore = v);
	public static void SetShowCache(bool value) => Set(ShowCache, value, v => ShowCache = v);
	public static void SetSortBy(AssetSortBy value) => Set(SortBy, value, v => SortBy = v);
	public static void SetFoldersFirst(bool value) => Set(FoldersFirst, value, v => FoldersFirst = v);
	public static void SetSearchScope(AssetSearchScope value) => Set(SearchScope, value, v => SearchScope = v);
	public static void SetConfirmDelete(bool value) => Set(ConfirmDelete, value, v => ConfirmDelete = v);

	public static void SetHiddenPatterns(IReadOnlyList<string> value) {
		var clean = value.Select(p => p.Trim()).Where(p => p.Length > 0).Distinct().ToList();
		if (clean.SequenceEqual(HiddenPatterns)) return;
		HiddenPatterns = clean;
		RebuildHiddenRegexes();
		Commit();
	}

	public static bool IsHidden(string name) {
		foreach (var regex in s_hiddenRegexes)
			if (regex.IsMatch(name))
				return true;
		return false;
	}

	internal static void ScheduleSave() {
		if (s_saveTimer is null) {
			s_saveTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(400) };
			s_saveTimer.Tick += (_, _) => {
				s_saveTimer.Stop();
				Save();
			};
		}

		s_saveTimer.Stop();
		s_saveTimer.Start();
	}

	public static bool Save() {
		s_saveTimer?.Stop();
		if (FindProjectFile() is not { } path) {
			Saved?.Invoke(false);
			return false;
		}

		try {
			var root = TomlSerializer.Deserialize<TomlTable>(File.ReadAllText(path)) ?? [];
			if (!root.TryGetValue(EditorKey, out var e) || e is not TomlTable editor) {
				editor = new TomlTable();
				root[EditorKey] = editor;
			}

			var section = new TomlTable {
				["show_tags"] = ShowTags,
				["show_type_badge"] = ShowTypeBadge,
				["card_size"] = CardSize.ToString(),
				["show_core"] = ShowCore,
				["show_cache"] = ShowCache,
				["sort_by"] = SortBy.ToString(),
				["folders_first"] = FoldersFirst,
				["search_scope"] = SearchScope.ToString(),
				["confirm_delete"] = ConfirmDelete
			};

			var patterns = new TomlArray();
			foreach (var pattern in HiddenPatterns) patterns.Add(pattern);
			section["hidden_patterns"] = patterns;

			var tags = new TomlTableArray();
			foreach (var tag in Tags)
				tags.Add(new TomlTable {
					["id"] = tag.Id,
					["name"] = tag.Name,
					["color"] = $"#{tag.Color.R:x2}{tag.Color.G:x2}{tag.Color.B:x2}"
				});
			section["tags"] = tags;

			editor[SectionKey] = section;
			File.WriteAllText(path, TomlSerializer.Serialize(root));
			Saved?.Invoke(true);
			return true;
		} catch {
			Saved?.Invoke(false);
			return false;
		}
	}

	private static void Set<T>(T current, T value, Action<T> assign) {
		if (EqualityComparer<T>.Default.Equals(current, value)) return;
		assign(value);
		Commit();
	}

	private static void Commit() {
		Changed?.Invoke();
		ScheduleSave();
	}

	private static void ApplyDefaults() {
		ShowTags = true;
		ShowTypeBadge = true;
		CardSize = AssetCardSize.Medium;
		ShowCore = true;
		ShowCache = false;
		SortBy = AssetSortBy.Name;
		FoldersFirst = true;
		SearchScope = AssetSearchScope.Everywhere;
		ConfirmDelete = true;
		HiddenPatterns = [];
		s_hiddenRegexes = [];
	}

	private static void RebuildHiddenRegexes() {
		s_hiddenRegexes = HiddenPatterns
			.Select(p => new Regex(
				"^" + Regex.Escape(p).Replace(@"\*", ".*").Replace(@"\?", ".") + "$",
				RegexOptions.IgnoreCase | RegexOptions.CultureInvariant))
			.ToList();
	}

	private static string? FindProjectFile() {
		if (!ProjectContext.IsInitialized) return null;
		return Directory.EnumerateFiles(ProjectContext.ProjectPath, "*.toast").FirstOrDefault();
	}

	private static bool GetBool(TomlTable table, string key, bool fallback) {
		return table.TryGetValue(key, out var value) && value is bool b ? b : fallback;
	}

	private static T GetEnum<T>(TomlTable table, string key, T fallback) where T : struct, Enum {
		return table.TryGetValue(key, out var value) && Enum.TryParse<T>(value?.ToString(), true, out var parsed)
			? parsed
			: fallback;
	}
}
