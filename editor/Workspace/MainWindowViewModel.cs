using System.Collections.Generic;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Controls;
using editor.Assets;
using editor.Components.Modals;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public partial class MainWindowViewModel : ViewModelBase {
	private readonly DockFactory m_dockFactory;
	private readonly ToastEngine m_toast;
	private readonly ToastZoneFactory m_toastZoneFactory;

	// keyed by workspace handle so we can clean up the dict when a tab closes
	private readonly Dictionary<ulong, WorkspaceViewModel> m_workspaces = [];

	private ulong m_activeWorkspaceHandle;

	[ObservableProperty] private bool m_toastZoneActive;

	private bool m_toastZonePinned;

	public MainWindowViewModel(ToastEngine toast) {
		m_toast = toast;

		m_dockFactory = new DockFactory();
		MainLayout = m_dockFactory.CreateLayout();
		m_dockFactory.InitLayout(MainLayout);

		m_toastZoneFactory = new ToastZoneFactory();
		ToastZoneLayout = m_toastZoneFactory.CreateLayout();
		m_toastZoneFactory.InitLayout(ToastZoneLayout);

		m_dockFactory.DockableClosed += (_, e) => {
			if (e.Dockable is WorkspaceViewModel ws) m_workspaces.Remove(ws.Handle);
			SyncActiveWorkspace();
		};

		m_dockFactory.ActiveDockableChanged += (_, _) => SyncActiveWorkspace();

		WorkspaceState.Modified += () => {
			if (m_dockFactory.ActiveWorkspace is { } ws) ws.IsModified = true;
		};
	}

	public IRootDock MainLayout { get; set; }
	public IRootDock ToastZoneLayout { get; set; }

	// tells the engine which workspace is focused so it routes input and viewport updates to the right one
	// also clears the hierarchy when no workspace is open
	private void SyncActiveWorkspace() {
		var handle = m_dockFactory.ActiveWorkspace?.Handle ?? 0;
		if (handle == m_activeWorkspaceHandle) return;
		m_activeWorkspaceHandle = handle;

		Events.Send(new SetActiveWorkspace { Handle = handle });
		if (handle == 0) m_dockFactory.Hierarchy?.Clear();
	}

	public void ShowToastZone(bool active) {
		if (m_toastZonePinned) return;
		ToastZoneActive = active;
	}

	public void PinToastZone() {
		m_toastZonePinned = !m_toastZonePinned;
		ToastZoneActive = m_toastZonePinned;
	}

	[RelayCommand]
	private async Task NewNode(Window parent) {
		var popup = new NodeTypePickerModal();
		var result = await popup.ShowDialog<string?>(parent);
		if (result is null) return;

		if (WorkspaceViewModel.CreateNew(m_toast, result) is not { } ws) return;
		m_workspaces[ws.Handle] = m_dockFactory.AddWorkspace(ws);
		ws.IsModified = true;
		SyncActiveWorkspace();
	}

	[RelayCommand]
	private async Task OpenNodeFile(Window parent) {
		if (TopLevel.GetTopLevel(parent) is not { } topLevel) return;

		var startFolder = await topLevel.StorageProvider.TryGetFolderFromPathAsync(ProjectContext.AssetsPath);
		var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions {
			Title = "Open Node File",
			AllowMultiple = false,
			SuggestedStartLocation = startFolder,
			FileTypeFilter = [
				new FilePickerFileType("Toast Node") { Patterns = ["*.tnode"] }
			]
		});

		if (files.Count == 0) return;

		var realPath = files[0].Path.LocalPath;
		var header = MetaFile.ReadHeader(realPath);
		if (header is null) return;

		var virtualPath = ProjectContext.ToVirtual(realPath);
		if (virtualPath is null) return;

		if (WorkspaceViewModel.OpenFile(m_toast, header.Uid, virtualPath) is not { } ws) return;
		m_workspaces[ws.Handle] = m_dockFactory.AddWorkspace(ws);
		SyncActiveWorkspace();
	}

	[RelayCommand]
	private void CloseNodeFile() {
		if (m_dockFactory.ActiveWorkspace is { } ws) m_dockFactory.CloseDockable(ws);
	}
}
