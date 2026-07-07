using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
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
	private readonly AutosaveService m_autosave;
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

		WorkspaceViewModel.PlayModeChanged += () => {
			SaveCurrentNodeCommand.NotifyCanExecuteChanged();
			SaveCurrentNodeAsCommand.NotifyCanExecuteChanged();
			SaveAllNodesCommand.NotifyCanExecuteChanged();
		};

		m_autosave = new AutosaveService(EnumerateAutosavables);
	}

	public IRootDock MainLayout { get; set; }
	public IRootDock ToastZoneLayout { get; set; }

	private IEnumerable<IAutosavable> EnumerateAutosavables() {
		foreach (var ws in m_workspaces.Values) yield return ws;
		if (m_dockFactory.GenericEditorVm is { } generic) yield return generic;
		if (m_dockFactory.SchemaEditorVm is { } schema) yield return schema;
		if (m_toastZoneFactory.CurveEditorVm is { } curve) yield return curve;
		if (m_toastZoneFactory.HapticsEditorVm is { } haptics) yield return haptics;
	}

	[RelayCommand]
	private Task AutosaveNow() {
		return m_autosave.RequestAutosave();
	}

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

	private async void OnEditorOpenRequested(AssetFile file) {
		if (file.Definition is not { CanBeEdited: true } def) return;
		if (file.Uid is not { } uid) return;
		if (!AssetDatabase.TryResolve(uid, out var virtualPath, out _)) return;

		var recoverPath = await AutosaveService.TryRecoverAsync(uid, virtualPath);

		switch (def.EditorTool) {
			case "GenericEditor":
				m_dockFactory.OpenGenericEditor(uid, virtualPath, def, recoverPath);
				GenericEditorVisible = true; // ShowRightTool already ran; IsToolVisible = true → no re-toggle
				break;
			case "SchemaEditor":
				m_dockFactory.OpenSchemaEditor(uid, virtualPath, recoverPath);
				SchemaEditorVisible = true;
				break;
			case "NodeEditor":
				OpenWorkspaceFile(uid, virtualPath, recoverPath);
				break;
			case "CurveEditor":
				if (m_toastZoneFactory.CurveEditorVm is { } curveVm) {
					_ = OpenToastEditorAsync(curveVm, uid, virtualPath, def, recoverPath);
					CurveEditorVisible = true;
				}

				break;
			case "HapticsEditor":
				if (m_toastZoneFactory.HapticsEditorVm is { } hapticsVm) {
					_ = OpenToastEditorAsync(hapticsVm, uid, virtualPath, def, recoverPath);
					HapticsEditorVisible = true;
				}

				break;
		}
	}

	private void OpenWorkspaceFile(string uid, string virtualPath, string? recoverPath) {
		var recoverVirtual = recoverPath is null ? null : ProjectContext.ToVirtual(recoverPath);
		if (WorkspaceViewModel.OpenFile(m_toast, uid, virtualPath, recoverVirtual) is not { } ws) return;
		m_workspaces[ws.Handle] = m_dockFactory.AddWorkspace(ws);
		if (recoverVirtual is not null) ws.IsModified = true; // recovered content is unsaved by definition
		SyncActiveWorkspace();
	}

	private async Task OpenToastEditorAsync<T>(
		T editor, string uid, string virtualPath, BaseAsset definition, string? contentSourceRealPath = null)
		where T : Tool, IToastZoneEditor {
		if (editor.IsDirty && !await editor.ConfirmCloseCurrentAsync()) return;
		editor.OpenFile(uid, virtualPath, definition, contentSourceRealPath);
		m_toastZoneFactory.ShowTool(editor);

		// pin the zone so it stays up while editing
		m_toastZonePinned = true;
		ToastZoneActive = true;
	}

	private void SyncActiveWorkspace() {
		// a playing tab routes to its temporary play clone, not the frozen editing workspace
		var handle = m_dockFactory.ActiveWorkspace?.EffectiveHandle ?? 0;
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
	private async Task NewNode() {
		if (App.MainWindow is not { } owner) return;
		var popup = new NodeTypeTree();
		var result = await popup.ShowDialog<string?>(owner);
		if (result is null) return;

		if (WorkspaceViewModel.CreateNew(m_toast, result) is not { } ws) return;
		m_workspaces[ws.Handle] = m_dockFactory.AddWorkspace(ws);
		ws.IsModified = true;
		SyncActiveWorkspace();
	}

	[RelayCommand]
	private async Task OpenNodeFile() {
		if (App.MainWindow is not { } owner) return;
		var uid = await new AssetList("Node").ShowDialog<string?>(owner);
		if (uid is null) return;

		if (!AssetDatabase.TryResolve(uid, out var virtualPath, out _)) return;

		var recoverPath = await AutosaveService.TryRecoverAsync(uid, virtualPath);
		OpenWorkspaceFile(uid, virtualPath, recoverPath);
	}

	[RelayCommand]
	private void CloseNodeFile() {
		if (m_dockFactory.ActiveWorkspace is { } ws) m_dockFactory.CloseDockable(ws);
	}

	// saving is locked while any tab is in play mode
	private static bool CanSave() {
		return !WorkspaceViewModel.AnyPlayActive;
	}

	[RelayCommand(CanExecute = nameof(CanSave))]
	private async Task SaveCurrentNode() {
		if (m_dockFactory.ActiveWorkspace is { } ws) await ws.Save();
	}

	[RelayCommand(CanExecute = nameof(CanSave))]
	private async Task SaveCurrentNodeAs() {
		if (m_dockFactory.ActiveWorkspace is { } ws) await ws.SaveAs();
	}

	[RelayCommand(CanExecute = nameof(CanSave))]
	private async Task SaveAllNodes() {
		foreach (var ws in m_workspaces.Values) await ws.Save();
	}

	[RelayCommand]
	private async Task ReloadGame() {
		var tasks = new List<LoaderTask>();
		string toastPath = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "../toast_engine"));
		var cmakeGenerator = OperatingSystem.IsWindows() ? "-G \"Visual Studio 18 2026\"" : "-G \"Ninja\"";
		
		tasks.Add(LoaderTask.Run("cmake lib/ -B cache://cmake_cache", "cmake", $"lib/ -B .toast/cmake_cache {cmakeGenerator} -DTOAST_PATH={toastPath}"));
		tasks.Add(LoaderTask.Run("cmake --build cache://cmake_cache", "cmake", "--build .toast/cmake_cache"));
		// TODO: release game DLL on cache://game_temp.so
		// TODO: overwite game DLL with new on ProjectPath/build
		// TODO: load game DLL back again
	}
	
	[RelayCommand]
	private async Task CompileGameRelease() {
		var tasks = new List<LoaderTask>();
		string playerPath = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", "..", "tools", "player"));
		string toastPath = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "../toast_engine"));
		string outputDirectory = Path.GetFullPath(Path.Combine(ProjectContext.ProjectPath, "build"));
		var cmakeGenerator = OperatingSystem.IsWindows() ? "-G \"Visual Studio 18 2026\"" : "-G \"Ninja\"";

		async Task CopyDlls(Action<string> log) {
			string binPath = Path.Combine(toastPath, "bin");
			string libPath = Path.Combine(outputDirectory, "lib");
			string libExt = OperatingSystem.IsWindows() ? ".dll" : ".so";
			string exeExt = OperatingSystem.IsWindows() ? ".exe" : "";
			
		   string[] sources = [binPath, libPath];
		   string[] extensions = new[] { libExt, exeExt }.Where(ext => !string.IsNullOrEmpty(ext)).ToArray();

		   foreach (string source in sources) {
		       if (!Directory.Exists(source)) {
		           log($"error: Directory {source} doesnt exist");
		           continue;
		       }
		       
		       var files = Directory.EnumerateFiles(source, "*.*", SearchOption.TopDirectoryOnly)
		                            .Where(file => extensions.Contains(Path.GetExtension(file), StringComparer.OrdinalIgnoreCase));

		       foreach (string file in files) {
		           string destFile = Path.Combine(outputDirectory, Path.GetFileName(file));
		           log($"Copying {Path.GetFileName(file)} a {outputDirectory}");
		           await Task.Run(() => File.Copy(file, destFile, overwrite: true));
		       }
		   }
		}
		
		tasks.Add(LoaderTask.Run("dotnet publish engine://tools/player -c Release", "dotnet", $"publish {playerPath} -c Release p:PublishAot=true -p:PublishSingleFile=true -p:OptimizationPreference=Speed -o \"{outputDirectory}\""));
		// TODO: This should be on release
		tasks.Add(LoaderTask.Run("cmake lib/ -B cache://cmake_cache", "cmake", $"lib/ -B .toast/cmake_cache {cmakeGenerator} -DTOAST_PATH={toastPath}"));
		tasks.Add(LoaderTask.Run("cmake --build cache://cmake_cache", "cmake", "--build .toast/cmake_cache"));
		tasks.Add(LoaderTask.Do("copy libraries --path build://", CopyDlls));
		// TODO: Pack assets
		// TODO: Pack core
	}
	
}
