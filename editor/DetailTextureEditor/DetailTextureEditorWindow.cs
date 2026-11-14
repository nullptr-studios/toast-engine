using System.Collections.Generic;
using Avalonia.Controls;
using editor.Assets;
using editor.Engine;
using editor.Workspace;

namespace editor.DetailTextureEditor;

public sealed class DetailTextureEditorWindow : Window {
	private static readonly Dictionary<string, DetailTextureEditorWindow> s_open = new();

	private readonly DetailTextureEditorViewModel m_vm;
	private bool m_confirmedClose;

	private DetailTextureEditorWindow(DetailTextureEditorViewModel vm) {
		m_vm = vm;
		Title = vm.Title;
		Width = 1400;
		Height = 860;
		MinWidth = 900;
		MinHeight = 520;
		WindowStartupLocation = WindowStartupLocation.CenterOwner;
		DataContext = vm;
		Content = new DetailTextureEditorView { DataContext = vm };

		Activated += (_, _) => m_vm.TakeEngine();
		vm.PropertyChanged += (_, e) => {
			if (e.PropertyName == nameof(DetailTextureEditorViewModel.Title)) Title = vm.Title;
		};

		// Unsaved changes ask first
		Closing += async (_, e) => {
			if (m_confirmedClose || !vm.IsDirty) return;
			e.Cancel = true;
			if (!await vm.ConfirmCloseAsync()) return;
			m_confirmedClose = true;
			Close();
		};
		Closed += (_, _) => {
			s_open.Remove(m_vm.Uid);
			m_vm.Dispose();
			MainWindowViewModel.Current?.ReclaimEngine();
		};
	}

	public static void Open(string uid) {
		if (s_open.TryGetValue(uid, out var existing)) {
			existing.Activate();
			return;
		}

		if (MainWindowViewModel.Current is not { } main || main.Engine is not { } engine) return;
		if (!AssetDatabase.TryResolve(uid, out var virtualPath, out _)) {
			Log.Warn($"Detail Texture Editor: no asset with uid {uid}");
			return;
		}

		var workspace = WorkspaceViewModel.CreateNew(engine, "toast::ProceduralVoxel");
		if (workspace is null) {
			Log.Warn($"Detail Texture Editor: could not create a preview workspace for {virtualPath}");
			return;
		}

		var window = new DetailTextureEditorWindow(new DetailTextureEditorViewModel(workspace, uid, virtualPath));
		s_open[uid] = window;
		if (App.MainWindow is { } owner) window.Show(owner);
		else window.Show();
	}
}
