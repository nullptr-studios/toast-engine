using System;
using System.Collections.Generic;
using Avalonia.Controls;
using editor.Assets;
using editor.Engine;
using editor.Workspace;

namespace editor.VoxelEditor;

public sealed class VoxelEditorWindow : Window {
	private static readonly Dictionary<string, VoxelEditorWindow> s_open = new();

	private readonly VoxelEditorViewModel m_vm;
	private bool m_confirmedClose;

	private VoxelEditorWindow(VoxelEditorViewModel vm) {
		m_vm = vm;
		Title = vm.Title;
		Width = 1500;
		Height = 900;
		MinWidth = 900;
		MinHeight = 560;
		WindowStartupLocation = WindowStartupLocation.CenterOwner;
		DataContext = vm;
		Content = new VoxelEditorView { DataContext = vm };

		Activated += (_, _) => m_vm.TakeEngine();
		vm.PropertyChanged += (_, e) => {
			if (e.PropertyName == nameof(VoxelEditorViewModel.Title)) Title = vm.Title;
		};

		// Unsaved changes ask first we dont want to loose data
		Closing += async (_, e) => {
			if (m_confirmedClose || !vm.Workspace.IsModified) return;
			e.Cancel = true;
			if (!await vm.ConfirmCloseAsync()) return;
			m_confirmedClose = true;
			Close();
		};
		Closed += (_, _) => {
			s_open.Remove(m_vm.PrefabUid);
			m_vm.Dispose();
			MainWindowViewModel.Current?.ReclaimEngine();
		};
	}

	public static void Open(string prefabUid, ulong sourceWorkspace = 0, string sourceInstance = "") {
		if (s_open.TryGetValue(prefabUid, out var existing)) {
			existing.Activate();
			return;
		}

		if (MainWindowViewModel.Current is not { } main) return;
		if (!AssetDatabase.TryResolve(prefabUid, out var virtualPath, out _)) {
			Log.Warn($"Voxel Editor: no asset with uid {prefabUid}");
			return;
		}

		// the engine opens an asset once
		var workspace = main.FindOpenWorkspace(prefabUid);
		var owns = workspace is null;
		workspace ??= WorkspaceViewModel.OpenFile(main.Engine, prefabUid, virtualPath);
		if (workspace is null) {
			Log.Warn($"Voxel Editor: could not open {virtualPath}");
			return;
		}

		var window = new VoxelEditorWindow(new VoxelEditorViewModel(workspace, owns, prefabUid, sourceWorkspace, sourceInstance));
		s_open[prefabUid] = window;
		if (App.MainWindow is { } owner) window.Show(owner);
		else window.Show();
	}
}
