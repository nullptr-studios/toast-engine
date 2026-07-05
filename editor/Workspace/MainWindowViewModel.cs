using System.Collections.Generic;
using System.Threading.Tasks;
using Avalonia.Controls;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Controls;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.Editors;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public partial class MainWindowViewModel : ViewModelBase {
	private readonly DockFactory m_dockFactory;
	private readonly ToastEngine m_toast;
	private readonly ToastZoneFactory m_toastZoneFactory;

	private readonly Dictionary<ulong, WorkspaceViewModel> m_workspaces = [];
	private ulong m_activeWorkspaceHandle;
	[ObservableProperty] private bool m_curveEditorVisible = true;
	[ObservableProperty] private bool m_genericEditorVisible;
	[ObservableProperty] private bool m_hapticsEditorVisible = true;

	[ObservableProperty] private bool m_hierarchyVisible = true;
	[ObservableProperty] private bool m_inspectorVisible = true;
	[ObservableProperty] private bool m_logsVisible = true;
	[ObservableProperty] private bool m_schemaEditorVisible;

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
			if (e.Dockable == m_dockFactory.Hierarchy) m_hierarchyVisible = false;
			if (e.Dockable == m_dockFactory.Inspector) m_inspectorVisible = false;
			if (e.Dockable == m_dockFactory.GenericEditorVm) m_genericEditorVisible = false;
			if (e.Dockable == m_dockFactory.SchemaEditorVm) m_schemaEditorVisible = false;

			OnPropertyChanged(nameof(HierarchyVisible));
			OnPropertyChanged(nameof(InspectorVisible));
			OnPropertyChanged(nameof(GenericEditorVisible));
			OnPropertyChanged(nameof(SchemaEditorVisible));

			if (m_workspaces.Count == 0) {
				m_activeWorkspaceHandle = 0;
				m_dockFactory.Hierarchy?.Clear();
				Events.Send(new SetActiveWorkspace { Handle = 0 });
			} else {
				m_activeWorkspaceHandle = 0;
				SyncActiveWorkspace();
			}
		};

		m_toastZoneFactory.DockableClosed += (_, e) => {
			if (e.Dockable == m_toastZoneFactory.LogsVm) m_logsVisible = false;
			if (e.Dockable == m_toastZoneFactory.HapticsEditorVm) m_hapticsEditorVisible = false;
			if (e.Dockable == m_toastZoneFactory.CurveEditorVm) m_curveEditorVisible = false;

			OnPropertyChanged(nameof(LogsVisible));
			OnPropertyChanged(nameof(HapticsEditorVisible));
			OnPropertyChanged(nameof(CurveEditorVisible));
		};

		m_dockFactory.ActiveDockableChanged += (_, _) => SyncActiveWorkspace();

		WorkspaceState.Modified += () => {
			if (m_dockFactory.ActiveWorkspace is { } ws) ws.IsModified = true;
		};

		m_dockFactory.SchemaEditorVm!.SchemaSaved +=
			path => m_dockFactory.GenericEditorVm?.RefreshFromSchema(path);

		EditorManager.OpenRequested += OnEditorOpenRequested;
	}

	public IRootDock MainLayout { get; set; }
	public IRootDock ToastZoneLayout { get; set; }

	partial void OnHierarchyVisibleChanged(bool value) {
		if (value != m_dockFactory.IsToolVisible("Hierarchy"))
			m_dockFactory.ToggleTool("Hierarchy");
	}

	partial void OnInspectorVisibleChanged(bool value) {
		if (value != m_dockFactory.IsToolVisible("Inspector"))
			m_dockFactory.ToggleTool("Inspector");
	}

	partial void OnGenericEditorVisibleChanged(bool value) {
		if (value != m_dockFactory.IsToolVisible("GenericEditor"))
			m_dockFactory.ToggleTool("GenericEditor");
	}

	partial void OnSchemaEditorVisibleChanged(bool value) {
		if (value != m_dockFactory.IsToolVisible("SchemaEditor"))
			m_dockFactory.ToggleTool("SchemaEditor");
	}

	partial void OnLogsVisibleChanged(bool value) {
		if (value != m_toastZoneFactory.IsToolVisible("Logs"))
			m_toastZoneFactory.ToggleTool("Logs");
	}

	partial void OnHapticsEditorVisibleChanged(bool value) {
		if (value != m_toastZoneFactory.IsToolVisible("Haptics"))
			m_toastZoneFactory.ToggleTool("Haptics");
	}

	partial void OnCurveEditorVisibleChanged(bool value) {
		if (value != m_toastZoneFactory.IsToolVisible("Curve"))
			m_toastZoneFactory.ToggleTool("Curve");
	}

	private void OnEditorOpenRequested(AssetFile file) {
		if (file.Definition is not { CanBeEdited: true } def) return;
		if (file.Uid is not { } uid) return;
		if (!AssetDatabase.TryResolve(uid, out var virtualPath, out _)) return;

		switch (def.EditorTool) {
			case "GenericEditor":
				m_dockFactory.OpenGenericEditor(uid, virtualPath, def);
				GenericEditorVisible = true; // ShowRightTool already ran; IsToolVisible = true → no re-toggle
				break;
			case "SchemaEditor":
				m_dockFactory.OpenSchemaEditor(uid, virtualPath);
				SchemaEditorVisible = true;
				break;
			case "NodeEditor":
				if (WorkspaceViewModel.OpenFile(m_toast, uid, virtualPath) is not { } ws) break;
				m_workspaces[ws.Handle] = m_dockFactory.AddWorkspace(ws);
				SyncActiveWorkspace();
				break;
			case "CurveEditor":
				if (m_toastZoneFactory.CurveEditorVm is { } curveVm) {
					_ = OpenToastEditorAsync(curveVm, uid, virtualPath, def);
					CurveEditorVisible = true;
				}

				break;
			case "HapticsEditor":
				if (m_toastZoneFactory.HapticsEditorVm is { } hapticsVm) {
					_ = OpenToastEditorAsync(hapticsVm, uid, virtualPath, def);
					HapticsEditorVisible = true;
				}

				break;
		}
	}

	private async Task OpenToastEditorAsync<T>(T editor, string uid, string virtualPath, BaseAsset definition)
		where T : Tool, IToastZoneEditor {
		if (editor.IsDirty && !await editor.ConfirmCloseCurrentAsync()) return;
		editor.OpenFile(uid, virtualPath, definition);
		m_toastZoneFactory.ShowTool(editor);

		// pin the zone so it stays up while editing
		m_toastZonePinned = true;
		ToastZoneActive = true;
	}

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
