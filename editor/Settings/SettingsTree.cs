//
// SettingsTree.cs by Xein
// 24 Nov 2026
//

using System;
using System.Collections.Generic;
using System.Linq;
using editor.Engine;

namespace editor.Settings;

public sealed class SettingsCard {
	public required string Key { get; init; }
	public required string Title { get; init; }

	public List<SettingDescriptor> Normal { get; } = [];

	public List<SettingDescriptor> Advanced { get; } = [];
	public List<SettingDescriptor> Debug { get; } = [];

	public IEnumerable<SettingDescriptor> All => Normal.Concat(Advanced).Concat(Debug);
}

public sealed class SettingsPageModel {
	public required string Key { get; init; }
	public required string Title { get; init; }
	public List<SettingsCard> Cards { get; } = [];
}

public static class SettingsTree {
	public const string EditorPage = "editor";
	public const string ProjectPage = "project";
	public const string GamePage = "game";

	private static readonly HashSet<string> s_custom = ["physics.layers.names", "physics.layers.abbreviations"];

	private static readonly Dictionary<string, string> s_pageOfGroup = new() {
		["project"] = ProjectPage,
		["gameplay"] = GamePage,
		["ui"] = "ui"
	};

	private static readonly Dictionary<string, string> s_pageOfKey = new() {
		["project.max_tick_rate"] = GamePage
	};

	private static readonly Dictionary<string, string> s_pageTitles = new() {
		[ProjectPage] = "Project",
		[GamePage] = "Game",
		["ui"] = "UI",
		["renderer"] = "Rendering",
		[EditorPage] = "Editor"
	};

	private static readonly HashSet<string> s_acronyms = new(StringComparer.OrdinalIgnoreCase) {
		"gi", "ui", "hdr", "ssr", "ssao", "taa", "fxaa", "rt", "ao", "lod", "fov", "ik", "hud", "vfx", "sdf", "pbr"
	};

	public static bool IsCustom(string key) => s_custom.Contains(key);

	public static string GroupOf(string key) {
		var dot = key.IndexOf('.');
		return dot < 0 ? key : key[..dot];
	}

	public static string IdOf(string key) {
		var dot = key.IndexOf('.');
		return dot < 0 ? key : key[(dot + 1)..];
	}

	public static string Pretty(string segment) {
		if (segment.Length == 0) return segment;
		if (s_acronyms.Contains(segment)) return segment.ToUpperInvariant();
		var spaced = segment.Replace('_', ' ');
		return char.ToUpperInvariant(spaced[0]) + spaced[1..];
	}

	public static string PrettyPath(string path) => string.Join(" / ", path.Split('.', StringSplitOptions.RemoveEmptyEntries).Select(Pretty));

	public static string LabelOf(SettingDescriptor descriptor) {
		var id = IdOf(descriptor.Key);
		if (descriptor.Label.Length > 0 && descriptor.Label != id) return descriptor.Label;
		var dot = id.LastIndexOf('.');
		return Pretty(dot < 0 ? id : id[(dot + 1)..]);
	}

	public static string PageOf(string group) => s_pageOfGroup.TryGetValue(group, out var page) ? page : group;

	public static string PageOfKey(string key) => s_pageOfKey.TryGetValue(key, out var page) ? page : PageOf(GroupOf(key));

	public static string TitleOfPage(string page) => s_pageTitles.TryGetValue(page, out var title) ? title : Pretty(page);

	public static List<SettingsPageModel> Build(IEnumerable<SettingDescriptor> descriptors) {
		var all = descriptors.Where(d => !IsCustom(d.Key)).ToList();
		var pages = new List<SettingsPageModel>();

		SettingsPageModel PageFor(string key) {
			var existing = pages.FirstOrDefault(p => p.Key == key);
			if (existing is not null) return existing;
			var created = new SettingsPageModel { Key = key, Title = TitleOfPage(key) };
			pages.Add(created);
			return created;
		}

		var groupsPerPage = all
			.Where(d => d.Kind != SettingKind.User)
			.Select(d => (Group: GroupOf(d.Key), Page: PageOfKey(d.Key)))
			.Distinct()
			.GroupBy(d => d.Page)
			.ToDictionary(g => g.Key, g => g.Count());

		foreach (var descriptor in all) {
			var group = GroupOf(descriptor.Key);
			var id = IdOf(descriptor.Key);
			var subgroup = id.Contains('.') ? id[..id.LastIndexOf('.')] : string.Empty;

			var isPreference = descriptor.Kind == SettingKind.User;
			var page = PageFor(isPreference ? EditorPage : PageOfKey(descriptor.Key));
			var shared = groupsPerPage.GetValueOrDefault(page.Key) > 1 || (isPreference && group != "editor");

			var cardKey = $"{group}/{subgroup}";
			var title = subgroup.Length == 0
				? shared ? Pretty(group) : "General"
				: shared ? $"{Pretty(group)} / {PrettyPath(subgroup)}" : PrettyPath(subgroup);

			if (s_pageOfKey.ContainsKey(descriptor.Key)) title = "Timing";

			var card = page.Cards.FirstOrDefault(c => c.Key == cardKey);
			if (card is null) {
				card = new SettingsCard { Key = cardKey, Title = title };
				page.Cards.Add(card);
			}

			switch (descriptor.Kind) {
				case SettingKind.Advanced: card.Advanced.Add(descriptor); break;
				case SettingKind.Debug: card.Debug.Add(descriptor); break;
				default: card.Normal.Add(descriptor); break;
			}
		}

		foreach (var card in pages.SelectMany(p => p.Cards)) {
			if (card.Normal.Count > 0) continue;
			if (card.Advanced.Count > 0) {
				card.Normal.AddRange(card.Advanced);
				card.Advanced.Clear();
			} else {
				card.Normal.AddRange(card.Debug);
				card.Debug.Clear();
			}
		}

		var editor = pages.FirstOrDefault(p => p.Key == EditorPage);
		if (editor is not null) {
			pages.Remove(editor);
			pages.Add(editor);
		}

		return pages;
	}
}
