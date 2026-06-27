using System.Collections.Generic;
using System.Threading.Tasks;
using Avalonia.Controls;
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
			if (m_workspaces.Count == 0) {
				m_activeWorkspaceHandle = 0;
				m_dockFactory.Hierarchy?.Clear();
				Events.Send(new SetActiveWorkspace { Handle = 0 });
			} else {
				m_activeWorkspaceHandle = 0;
				SyncActiveWorkspace();
			}
		};

		m_dockFactory.ActiveDockableChanged += (_, _) => SyncActiveWorkspace();

		WorkspaceState.Modified += () => {
			if (m_dockFactory.ActiveWorkspace is { } ws) ws.IsModified = true;
		};
	}

	public IRootDock MainLayout { get; set; }
	public IRootDock ToastZoneLayout { get; set; }

	// tells the engine which workspace is focused so it routes input and viewport updates to the right one
	private void SyncActiveWorkspace() {
		var handle = m_dockFactory.ActiveWorkspace?.Handle ?? 0;
		if (handle == m_activeWorkspaceHandle) return;
		m_activeWorkspaceHandle = handle;
		Events.Send(new SetActiveWorkspace { Handle = handle });
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
		var popup = new NodeTypeTree();
		var result = await popup.ShowDialog<string?>(parent);
		if (result is null) return;

		if (WorkspaceViewModel.CreateNew(m_toast, result) is not { } ws) return;
		m_workspaces[ws.Handle] = m_dockFactory.AddWorkspace(ws);
		ws.IsModified = true;
		SyncActiveWorkspace();
	}

	[RelayCommand]
	private async Task OpenNodeFile(Window parent) {
		if (App.MainWindow is not { } owner) return;
		var uid = await new AssetList("Node").ShowDialog<string?>(owner);
		if (uid is null) return;

		if (!AssetDatabase.TryResolve(uid, out var virtualPath, out _)) return;

		if (WorkspaceViewModel.OpenFile(m_toast, uid, virtualPath) is not { } ws) return;
		m_workspaces[ws.Handle] = m_dockFactory.AddWorkspace(ws);
		SyncActiveWorkspace();
	}

	[RelayCommand]
	private void CloseNodeFile() {
		if (m_dockFactory.ActiveWorkspace is { } ws) m_dockFactory.CloseDockable(ws);
	}

	[RelayCommand]
	private async Task SaveCurrentNode() {
		if (m_dockFactory.ActiveWorkspace is { } ws) await ws.Save();
	}

	[RelayCommand]
	private async Task SaveCurrentNodeAs() {
		if (m_dockFactory.ActiveWorkspace is { } ws) await ws.SaveAs();
	}

	[RelayCommand]
	private async Task SaveAllNodes() {
		foreach (var ws in m_workspaces.Values) await ws.Save();
	}
}
