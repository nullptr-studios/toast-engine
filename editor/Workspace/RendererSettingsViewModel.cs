//
// RendererSettingsViewModel.cs
// 16 Aug 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Engine;

namespace editor.Workspace;

/// <summary>
/// The renderer's settings and bake actions, replacing the ImGui debug panel
/// </summary>
/// <remarks>
/// The rows are generated from whatever the engine declared under the "renderer." prefix, so a knob added in
/// C++ appears here with no change to this file. Another system wanting its own panel changes the prefix -
/// that is the whole extension point
///
/// Values are written through to the engine as they are edited, which is what makes the viewport respond
/// live. Persisting them is separate and explicit: Save writes the layer the editor is authoring, which for
/// the editor is the project layer that ships as the game's defaults
/// </remarks>
public partial class RendererSettingsViewModel : Tool {
	private const string KeyPrefix = "renderer.";

	/// A bake's progress is only visible through counters the render thread updates, so the panel polls while
	/// it is on screen rather than the renderer pushing an event for something only this window reads
	private readonly DispatcherTimer m_statusTimer;

	[ObservableProperty] private uint m_irradianceProbeCount;
	[ObservableProperty] private bool m_rayTracingSupported;
	[ObservableProperty] private string m_savePath = string.Empty;
	[ObservableProperty] private string m_statusMessage = string.Empty;
	[ObservableProperty] private uint m_staleIrradianceVolumes;
	[ObservableProperty] private uint m_staleReflectionProbes;

	public RendererSettingsViewModel() {
		Categories = [];

		m_statusTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1.0) };
		m_statusTimer.Tick += (_, _) => RefreshStatus();
	}

	public ObservableCollection<SettingCategory> Categories { get; }

	public bool HasStaleReflectionProbes => StaleReflectionProbes > 0;
	public bool HasStaleIrradianceVolumes => StaleIrradianceVolumes > 0;

	/// <summary>Rebuilds the rows from the engine's registry</summary>
	/// <remarks>
	/// Deferred until the panel is first shown rather than done in the constructor: the dock builds its tools
	/// as the shell starts, which is before the renderer exists and therefore before anything under
	/// "renderer." has been declared. Calling it again is how the panel picks up a later declaration
	/// </remarks>
	public void Load() {
		Categories.Clear();
		foreach (var category in SettingRowFactory.BuildCategories(KeyPrefix)) Categories.Add(category);

		SavePath = ToastSettings.ActivePath;
		RayTracingSupported = ToastRenderer.SupportsRayTracing;
		RefreshStatus();

		StatusMessage = Categories.Count == 0
			? "No renderer settings declared yet - open a workspace so the renderer starts."
			: string.Empty;
	}

	public void StartPolling() {
		if (Categories.Count == 0) Load();
		m_statusTimer.Start();
	}

	public void StopPolling() {
		m_statusTimer.Stop();
	}

	private void RefreshStatus() {
		StaleReflectionProbes = ToastRenderer.StaleReflectionProbes;
		StaleIrradianceVolumes = ToastRenderer.StaleIrradianceVolumes;
		IrradianceProbeCount = ToastRenderer.IrradianceProbeCount;
		OnPropertyChanged(nameof(HasStaleReflectionProbes));
		OnPropertyChanged(nameof(HasStaleIrradianceVolumes));
	}

	[RelayCommand]
	private void Save() {
		StatusMessage = ToastSettings.Save() ? $"Saved to {ToastSettings.ActivePath}" : "Save failed - see the log";
	}

	/// <summary>Drops every override in the active layer, exposing the defaults underneath</summary>
	[RelayCommand]
	private void ResetAll() {
		ToastSettings.ResetAll();
		foreach (var category in Categories)
		foreach (var row in category.Rows)
			row.Reload();

		StatusMessage = "Reset to defaults - not saved yet";
	}

	[RelayCommand]
	private void Reload() {
		Load();
		StatusMessage = "Reloaded from the engine";
	}

	[RelayCommand]
	private void BakeReflectionProbes() {
		ToastRenderer.BakeReflectionProbes();
		StatusMessage = "Baking reflection probes - six frames per probe, shown in the viewport";
	}

	[RelayCommand]
	private void BakeIrradianceVolumes() {
		ToastRenderer.BakeIrradianceVolumes();
		StatusMessage = IrradianceProbeCount > 0
			? $"Baking {IrradianceProbeCount} irradiance probes - {IrradianceProbeCount * 6} frames"
			: "No irradiance volumes registered in this scene";
	}
}
