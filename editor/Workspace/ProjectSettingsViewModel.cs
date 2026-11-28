//
// ProjectSettingsViewModel.cs
// 22 Sep 2026, rewritten 24 Nov 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Engine;
using editor.Settings;

namespace editor.Workspace;

public enum SettingsTab {
	General,
	Rendering,
	Physics,
	Editor,

	Group
}

public enum SettingsExtra {
	None,
	General,
	PhysicsLayers,
	Baking,
	AssetBrowser
}

public sealed class SettingsSection {
	public required string Name { get; init; }
	public required SettingsTab Tab { get; init; }

	public string? PageKey { get; init; }

	public string? CardKey { get; init; }

	public string? Category { get; init; }
	public SettingsExtra Extra { get; init; }

	public ObservableCollection<SettingsSection> Children { get; } = [];
}

public sealed partial class SettingsNavItem : ObservableObject {
	[ObservableProperty] private bool m_isExpanded = true;
	[ObservableProperty] private bool m_isSelected;

	public SettingsNavItem(SettingsSection section, int depth) {
		Section = section;
		Depth = depth;
	}

	public SettingsSection Section { get; }
	public int Depth { get; }
	public string Name => Section.Name;
	public bool HasChildren => Section.Children.Count > 0;
	public Thickness Indent => new(10 + (Depth * 20), 0, 0, 0);
}

public sealed partial class SettingsCardViewModel : ObservableObject {
	private readonly List<SettingRow> m_normal;
	private readonly List<SettingRow> m_advanced;
	private readonly List<SettingRow> m_debug;

	private static readonly string[] s_accents = ["Red", "Orange", "Yellow", "Green", "Cyan", "Blue", "Magenta"];

	private readonly string m_stateKey;
	private bool m_restoring;
	private bool m_searching;

	public SettingsCardViewModel(SettingsCard card, string pageKey, int index, ProjectInfoViewModel? info = null) {
		Key = card.Key;
		Title = info is not null ? "Project" : card.Title;
		Info = info;
		ColorKey = s_accents[index % s_accents.Length];
		m_stateKey = pageKey + "/" + card.Key;
		m_normal = card.Normal.Select(SettingRow.Create).ToList();
		m_advanced = card.Advanced.Select(SettingRow.Create).ToList();
		m_debug = card.Debug.Select(SettingRow.Create).ToList();

		// how it was left the last time
		m_restoring = true;
		var state = SettingsPanelState.Current;
		Expanded = state.Get("card:" + m_stateKey, true);
		AdvancedExpanded = state.Get("advanced:" + m_stateKey, false);
		DebugExpanded = state.Get("debug:" + m_stateKey, true);
		m_restoring = false;
	}

	[ObservableProperty] private bool m_advancedExpanded;
	[ObservableProperty] private bool m_debugExpanded = true;
	[ObservableProperty] private bool m_expanded = true;

	public string Key { get; }
	public string Title { get; }
	public string ColorKey { get; }

	public ProjectInfoViewModel? Info { get; }
	public bool HasInfo => Info is not null;

	partial void OnExpandedChanged(bool value) {
		if (!m_restoring) SettingsPanelState.Current.Set("card:" + m_stateKey, value, true);
	}

	partial void OnAdvancedExpandedChanged(bool value) {
		if (!m_restoring && !m_searching) SettingsPanelState.Current.Set("advanced:" + m_stateKey, value, false);
	}

	partial void OnDebugExpandedChanged(bool value) {
		if (!m_restoring && !m_searching) SettingsPanelState.Current.Set("debug:" + m_stateKey, value, true);
	}

	public ObservableCollection<SettingRow> Normal { get; } = [];
	public ObservableCollection<SettingRow> Advanced { get; } = [];
	public ObservableCollection<SettingRow> Debug { get; } = [];

	public bool HasAdvanced => Advanced.Count > 0;
	public bool HasDebug => Debug.Count > 0;
	public bool IsVisible => Normal.Count + Advanced.Count + Debug.Count > 0 || (Info is not null && !m_searching);

	public IEnumerable<SettingRow> AllRows => m_normal.Concat(m_advanced).Concat(m_debug);

	public void ApplyFilter(string query, bool showAdvanced, bool showDebug) {
		var searching = query.Length > 0;
		Fill(Normal, m_normal, query, true);
		Fill(Advanced, m_advanced, query, showAdvanced || searching);
		Fill(Debug, m_debug, query, showDebug || searching);

		m_searching = searching;
		if (searching) {
			if (Advanced.Count > 0) AdvancedExpanded = true;
			if (Debug.Count > 0) DebugExpanded = true;
		}

		OnPropertyChanged(nameof(HasAdvanced));
		OnPropertyChanged(nameof(HasDebug));
		OnPropertyChanged(nameof(IsVisible));
	}

	private static void Fill(ObservableCollection<SettingRow> target, List<SettingRow> source, string query, bool show) {
		var wanted = show ? source.Where(r => query.Length == 0 || r.Matches(query)).ToList() : [];
		if (target.SequenceEqual(wanted)) return;
		target.Clear();
		foreach (var row in wanted) target.Add(row);
	}
}

public partial class ProjectSettingsViewModel : Tool {
	public const string AssetBrowserCategory = "Asset Browser";
	public const string PhysicsLayersCategory = "Layers";
	public const string BakingCategory = "Baking";

	private static readonly HashSet<string> s_flatPages = ["project", "game", "assets"];

	private const string ProjectPage = "project";
	private const string PhysicsPage = "physics";
	private const string RendererPage = "renderer";

	private readonly DispatcherTimer m_refreshTimer;
	private List<SettingsPageModel> m_pages = [];

	[ObservableProperty] private uint m_irradianceProbeCount;
	[ObservableProperty] private bool m_rayTracingSupported;
	[ObservableProperty] private string m_savePath = string.Empty;
	[ObservableProperty] private string m_searchText = string.Empty;
	[ObservableProperty] private SettingsSection? m_selectedSection;
	[ObservableProperty] private uint m_staleIrradianceVolumes;
	[ObservableProperty] private uint m_staleReflectionProbes;
	[ObservableProperty] private string m_statusMessage = string.Empty;

	public ProjectSettingsViewModel() {
		Sections = [];
		Cards = [];

		m_refreshTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1.0) };
		m_refreshTimer.Tick += (_, _) => RefreshLive();

		AssetBrowserSettings.Saved += ok => {
			if (IsAssetBrowser) StatusMessage = ok ? $"Saved to {Path.GetFileName(SavePathOfProject())}" : "Save failed";
		};
	}

	public ProjectInfoViewModel ProjectInfo { get; } = new();

	public ObservableCollection<SettingsNavItem> NavRows { get; } = [];

	private readonly Dictionary<SettingsSection, SettingsNavItem> m_navItems = [];

	public AssetBrowserSettingsViewModel AssetBrowser { get; } = new();

	public PhysicsLayersViewModel PhysicsLayers { get; } = new();

	public ObservableCollection<SettingsSection> Sections { get; }

	public ObservableCollection<SettingsCardViewModel> Cards { get; }

	public SettingsExtra ActiveExtra => SelectedSection?.Extra ?? SettingsExtra.None;
	public bool IsPhysicsLayers => ActiveExtra == SettingsExtra.PhysicsLayers;

	public bool ShowPhysicsLayers => IsPhysicsLayers || SelectedSection is { Extra: SettingsExtra.None, PageKey: PhysicsPage, CardKey: null };
	public bool IsAssetBrowser => ActiveExtra == SettingsExtra.AssetBrowser;

	public bool ShowAssetBrowser => IsAssetBrowser || SelectedSection is { Extra: SettingsExtra.None, PageKey: SettingsTree.EditorPage, CardKey: null };

	public bool ShowBaking => ActiveExtra == SettingsExtra.Baking || (SelectedSection is { Extra: SettingsExtra.None, PageKey: RendererPage, CardKey: null });

	public bool ShowCards => ActiveExtra is SettingsExtra.None;

	public bool IsSearching => SearchText.Length > 0;
	public bool NothingToShow => ShowCards && Cards.Count == 0;

	public string EmptyMessage => IsSearching
		? "Nothing matches that."
		: "Nothing declared here yet. A system declares its parameters when it starts, so open a workspace.";

	public string SectionTitle => SelectedSection is null ? string.Empty : SelectedSection.CardKey is null ? SelectedSection.Name : $"{PageName(SelectedSection)} / {SelectedSection.Name}";

	public bool HasStaleReflectionProbes => StaleReflectionProbes > 0;
	public bool HasStaleIrradianceVolumes => StaleIrradianceVolumes > 0;

	public void Load() {
		var previous = SelectedSection;

		m_pages = SettingsTree.Build(ToastSettings.Enumerate());

		Sections.Clear();

		foreach (var page in m_pages.Where(p => p.Key != SettingsTree.EditorPage)) Sections.Add(BuildRoot(page));
		Sections.Add(BuildEditorRoot(m_pages.FirstOrDefault(p => p.Key == SettingsTree.EditorPage)));

		PhysicsLayers.Reload();
		SavePath = ToastSettings.ActivePath;
		RayTracingSupported = ToastRenderer.SupportsRayTracing;
		RefreshStatus();
		ProjectInfo.Load();

		SelectedSection = Flatten().FirstOrDefault(s => previous is not null && Same(s, previous)) ?? Sections[0];
		RebuildNav();
		RebuildCards();
	}

	public void StartPolling() {
		if (Sections.Count == 0) Load();
		m_refreshTimer.Start();
	}

	public void StopPolling() {
		m_refreshTimer.Stop();
	}

	public void SelectSection(SettingsTab tab, string? category = null) {
		if (Sections.Count == 0) Load();
		SelectedSection = Flatten().FirstOrDefault(s => s.Tab == tab && s.Category == category) ?? SelectedSection;
	}

	private IEnumerable<SettingsSection> Flatten() => Sections.SelectMany(root => new[] { root }.Concat(root.Children));

	private static bool Same(SettingsSection a, SettingsSection b) {
		return a.Tab == b.Tab && a.PageKey == b.PageKey && a.CardKey == b.CardKey && a.Extra == b.Extra && a.Category == b.Category;
	}

	private string PageName(SettingsSection section) {
		var root = Sections.FirstOrDefault(r => r.Children.Contains(section));
		return root?.Name ?? string.Empty;
	}

	private static SettingsSection BuildRoot(SettingsPageModel page) {
		var tab = page.Key switch {
			PhysicsPage => SettingsTab.Physics,
			RendererPage => SettingsTab.Rendering,
			_ => SettingsTab.Group
		};
		var root = new SettingsSection { Name = page.Title, Tab = tab, PageKey = page.Key };
		if (page.Cards.Count > 1 && !s_flatPages.Contains(page.Key))
			foreach (var card in page.Cards)
			root.Children.Add(new SettingsSection {
				Name = card.Title,
				Tab = tab,
				PageKey = page.Key,
				CardKey = card.Key,
				Category = card.Key
			});

		if (page.Key == PhysicsPage)
			root.Children.Add(new SettingsSection {
				Name = PhysicsLayersCategory,
				Tab = SettingsTab.Physics,
				PageKey = page.Key,
				Category = PhysicsLayersCategory,
				Extra = SettingsExtra.PhysicsLayers
			});

		if (page.Key == RendererPage)
			root.Children.Add(new SettingsSection {
				Name = BakingCategory,
				Tab = SettingsTab.Rendering,
				PageKey = page.Key,
				Category = BakingCategory,
				Extra = SettingsExtra.Baking
			});

		return root;
	}

	private static SettingsSection BuildEditorRoot(SettingsPageModel? preferences) {
		var root = new SettingsSection { Name = "Editor", Tab = SettingsTab.Editor, PageKey = SettingsTree.EditorPage };
		if (preferences is not null)
			foreach (var card in preferences.Cards)
				root.Children.Add(new SettingsSection {
					Name = card.Title,
					Tab = SettingsTab.Editor,
					PageKey = SettingsTree.EditorPage,
					CardKey = card.Key,
					Category = card.Key
				});

		root.Children.Add(new SettingsSection {
			Name = AssetBrowserCategory,
			Tab = SettingsTab.Editor,
			PageKey = SettingsTree.EditorPage,
			Category = AssetBrowserCategory,
			Extra = SettingsExtra.AssetBrowser
		});
		return root;
	}

	private static string SavePathOfProject() {
		return ProjectContext.IsInitialized
			? Directory.EnumerateFiles(ProjectContext.ProjectPath, "*.toast").FirstOrDefault() ?? ""
			: "";
	}

	private void RebuildCards() {
		Cards.Clear();

		if (SelectedSection is { Extra: SettingsExtra.None, PageKey: { } pageKey } section) {
			var page = m_pages.FirstOrDefault(p => p.Key == pageKey);
			if (page is not null)
				foreach (var card in page.Cards.Where(c => section.CardKey is null || c.Key == section.CardKey)) {
					var withInfo = pageKey == ProjectPage && card.Key == "project/";
					Cards.Add(new SettingsCardViewModel(card, pageKey, page.Cards.IndexOf(card), withInfo ? ProjectInfo : null));
				}
		}

		ApplyFilter();
	}

	private void ApplyFilter() {
		var query = SearchText.Trim();
		foreach (var card in Cards) card.ApplyFilter(query, true, true);

		foreach (var empty in Cards.Where(c => !c.IsVisible).ToList()) Cards.Remove(empty);

		OnPropertyChanged(nameof(NothingToShow));
		OnPropertyChanged(nameof(IsSearching));
		OnPropertyChanged(nameof(EmptyMessage));
	}

	partial void OnSelectedSectionChanged(SettingsSection? value) {
		RebuildCards();
		OnPropertyChanged(nameof(ActiveExtra));
		OnPropertyChanged(nameof(ShowPhysicsLayers));
		foreach (var item in m_navItems.Values) item.IsSelected = value is not null && item.Section == value;
		OnPropertyChanged(nameof(IsPhysicsLayers));
		OnPropertyChanged(nameof(IsAssetBrowser));
		OnPropertyChanged(nameof(ShowAssetBrowser));
		OnPropertyChanged(nameof(ShowBaking));
		OnPropertyChanged(nameof(ShowCards));
		OnPropertyChanged(nameof(SectionTitle));
		OnPropertyChanged(nameof(NothingToShow));
	}

	partial void OnSearchTextChanged(string value) => ReapplyFilter();

	private void ReapplyFilter() {
		RebuildCards();
		OnPropertyChanged(nameof(NothingToShow));
	}

	private void RebuildNav() {
		var open = m_navItems.Where(p => p.Value.Depth == 0).GroupBy(p => p.Key.Name + "/" + p.Key.PageKey)
			.ToDictionary(g => g.Key, g => g.First().Value.IsExpanded);
		m_navItems.Clear();
		foreach (var root in Sections) {
			var item = new SettingsNavItem(root, 0);
			if (open.TryGetValue(root.Name + "/" + root.PageKey, out var expanded)) item.IsExpanded = expanded;
			m_navItems[root] = item;
			foreach (var child in root.Children) m_navItems[child] = new SettingsNavItem(child, 1);
		}

		SyncNavRows();
	}

	private void SyncNavRows() {
		NavRows.Clear();
		foreach (var root in Sections) {
			NavRows.Add(m_navItems[root]);
			if (!m_navItems[root].IsExpanded) continue;
			foreach (var child in root.Children) NavRows.Add(m_navItems[child]);
		}

		foreach (var item in m_navItems.Values) item.IsSelected = SelectedSection is not null && item.Section == SelectedSection;
	}

	public void SelectNav(SettingsNavItem item) {
		SelectedSection = item.Section;
		if (item.Depth == 0 && !item.IsExpanded) {
			item.IsExpanded = true;
			SyncNavRows();
		}
	}

	public void ToggleNav(SettingsNavItem item) {
		item.IsExpanded = !item.IsExpanded;
		SyncNavRows();
	}

	private void RefreshLive() {
		foreach (var row in Cards.SelectMany(c => c.AllRows)) row.Reload(false);
		RefreshStatus();
	}

	private void RefreshStatus() {
		if (!ShowBaking) return;
		StaleReflectionProbes = ToastRenderer.StaleReflectionProbes;
		StaleIrradianceVolumes = ToastRenderer.StaleIrradianceVolumes;
		IrradianceProbeCount = ToastRenderer.IrradianceProbeCount;
		OnPropertyChanged(nameof(HasStaleReflectionProbes));
		OnPropertyChanged(nameof(HasStaleIrradianceVolumes));
	}

	[RelayCommand]
	private void Save() {
		StatusMessage = ToastSettings.Save() ? $"Saved to {ToastSettings.ActivePath}" : "Save failed";
	}

	[RelayCommand]
	private void ResetAll() {
		ToastSettings.ResetAll();
		foreach (var row in Cards.SelectMany(c => c.AllRows)) row.Reload();
		PhysicsLayers.Reload();
		StatusMessage = "Reset to defaults";
	}

	[RelayCommand]
	private void Reload() {
		Load();
		StatusMessage = "Reloaded from the engine";
	}

	[RelayCommand]
	private void ClearSearch() {
		SearchText = string.Empty;
	}

	[RelayCommand]
	private void BakeReflectionProbes() {
		ToastRenderer.BakeReflectionProbes();
		StatusMessage = "Baking reflection probes";
	}

	[RelayCommand]
	private void BakeIrradianceVolumes() {
		ToastRenderer.BakeIrradianceVolumes();
		StatusMessage = IrradianceProbeCount > 0
			? $"Baking {IrradianceProbeCount} irradiance probes"
			: "No irradiance volumes registered in this scene";
	}
}
