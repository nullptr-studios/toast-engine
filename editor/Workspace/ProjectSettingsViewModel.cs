//
// ProjectSettingsViewModel.cs
// 22 Sep 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Editors;
using editor.Engine;

namespace editor.Workspace;

public enum SettingsTab {
	General,
	Rendering,
	Physics
}

public sealed class SettingsSection {
	public required string Name { get; init; }
	public required SettingsTab Tab { get; init; }

	public string? Category { get; init; }

	public ObservableCollection<SettingsSection> Children { get; } = [];

	public bool IsRoot => Category is null;
}

public partial class ProjectSettingsViewModel : Tool {
	private const string RendererPrefix = "renderer.";
	private const string PhysicsPrefix = "physics.";

	private readonly DispatcherTimer m_statusTimer;

	[ObservableProperty] private uint m_irradianceProbeCount;
	[ObservableProperty] private bool m_rayTracingSupported;
	[ObservableProperty] private string m_savePath = string.Empty;
	[ObservableProperty] private SettingsSection? m_selectedSection;
	[ObservableProperty] private uint m_staleIrradianceVolumes;
	[ObservableProperty] private uint m_staleReflectionProbes;
	[ObservableProperty] private string m_statusMessage = string.Empty;

	public ProjectSettingsViewModel() {
		Sections = [];
		VisibleCategories = [];
		GeneralEditor = new GenericViewModel { Id = "ProjectSettingsGeneral", Title = "General" };

		m_statusTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1.0) };
		m_statusTimer.Tick += (_, _) => RefreshStatus();
	}

	public GenericViewModel GeneralEditor { get; }

	public ObservableCollection<SettingsSection> Sections { get; }

	public ObservableCollection<SettingCategory> VisibleCategories { get; }

	public SettingsTab ActiveTab => SelectedSection?.Tab ?? SettingsTab.General;

	public bool IsGeneral => ActiveTab == SettingsTab.General;
	public bool IsRendering => ActiveTab == SettingsTab.Rendering;
	public bool IsPhysics => ActiveTab == SettingsTab.Physics;

	public bool HasStaleReflectionProbes => StaleReflectionProbes > 0;
	public bool HasStaleIrradianceVolumes => StaleIrradianceVolumes > 0;

	public void Load() {
		var previous = SelectedSection;

		Sections.Clear();
		Sections.Add(BuildRoot("General", SettingsTab.General, []));
		Sections.Add(BuildRoot("Rendering", SettingsTab.Rendering, SettingRowFactory.BuildCategories(RendererPrefix)));
		Sections.Add(BuildRoot("Physics", SettingsTab.Physics, SettingRowFactory.BuildCategories(PhysicsPrefix)));

		SavePath = ToastSettings.ActivePath;
		RayTracingSupported = ToastRenderer.SupportsRayTracing;
		RefreshStatus();

		OpenProjectSettingsFile();

		SelectedSection = Sections
			                  .SelectMany(root => new[] { root }.Concat(root.Children))
			                  .FirstOrDefault(s => s.Tab == previous?.Tab && s.Category == previous?.Category)
		                  ?? Sections[0];
	}

	public void StartPolling() {
		if (Sections.Count == 0) Load();
		m_statusTimer.Start();
	}

	public void StopPolling() {
		m_statusTimer.Stop();
	}

	private static SettingsSection BuildRoot(string name, SettingsTab tab, List<SettingCategory> categories) {
		var root = new SettingsSection { Name = name, Tab = tab };
		foreach (var category in categories)
			root.Children.Add(new SettingsSection { Name = category.Name, Tab = tab, Category = category.Name });
		return root;
	}

	private void OpenProjectSettingsFile() {
		if (!ProjectContext.IsInitialized) return;

		var realPath = Directory.EnumerateFiles(ProjectContext.ProjectPath, "*.toast").FirstOrDefault();
		if (realPath is null) {
			StatusMessage = "No .toast project file found at the project root";
			return;
		}

		var virtualPath = ProjectContext.ToVirtual(realPath) ?? realPath;
		var definition = AssetTypeRegistry.All.OfType<ProjectSettingsAsset>().FirstOrDefault();
		if (definition is null) return;

		GeneralEditor.OpenFile(Path.GetFileNameWithoutExtension(realPath), virtualPath, definition);
	}

	private void RefreshVisibleCategories() {
		VisibleCategories.Clear();
		if (SelectedSection is not { } section || section.Tab == SettingsTab.General) return;

		var prefix = section.Tab == SettingsTab.Rendering ? RendererPrefix : PhysicsPrefix;
		foreach (var category in SettingRowFactory.BuildCategories(prefix)) {
			if (section.Category is null || section.Category == category.Name) VisibleCategories.Add(category);
		}
	}

	partial void OnSelectedSectionChanged(SettingsSection? value) {
		RefreshVisibleCategories();
		OnPropertyChanged(nameof(ActiveTab));
		OnPropertyChanged(nameof(IsGeneral));
		OnPropertyChanged(nameof(IsRendering));
		OnPropertyChanged(nameof(IsPhysics));
	}

	private void RefreshStatus() {
		if (!IsRendering) return;
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
		foreach (var category in VisibleCategories)
		foreach (var row in category.Rows)
			row.Reload();

		StatusMessage = "Reset to defaults";
	}

	[RelayCommand]
	private void Reload() {
		Load();
		StatusMessage = "Reloaded from the engine";
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
