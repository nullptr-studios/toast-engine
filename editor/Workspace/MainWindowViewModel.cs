using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Controls;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.Editors;
using editor.Engine;
using editor.Git;
using Lucide.Avalonia;
using Proto.Events;

namespace editor.Workspace;

public partial class MainWindowViewModel : ViewModelBase, IDisposable {
	private readonly AutosaveService m_autosave;

	public static MainWindowViewModel? Current { get; private set; }

	private readonly LayoutFile m_defaultLayout;
	private readonly DockFactory m_dockFactory;
	private readonly ToastEngine m_toast;
	private readonly ToastZoneFactory m_toastZoneFactory;
	private ProjectSettingsWindow? m_projectSettingsWindow;
	private CommitWindow? m_commitWindow;
	private LockArtworkWindow? m_lockArtworkWindow;
	private bool m_gitBusy;

	private readonly Dictionary<ulong, WorkspaceViewModel> m_workspaces = [];
	[ObservableProperty] private string m_activeLayoutName = LayoutStore.DefaultName;
	private ulong m_activeWorkspaceHandle;
	private bool m_applyingLayout;
	[ObservableProperty] private bool m_curveEditorVisible;
	[ObservableProperty] private bool m_genericEditorVisible;
	[ObservableProperty] private bool m_hapticsEditorVisible;
	[ObservableProperty] private bool m_paletteEditorVisible;

	[ObservableProperty] private bool m_hierarchyVisible = true;
	[ObservableProperty] private bool m_historyVisible;
	[ObservableProperty] private bool m_locksVisible;
	[ObservableProperty] private bool m_inspectorVisible = true;
	[ObservableProperty] private bool m_signalsVisible = true;
	[ObservableProperty] private bool m_logsVisible = true;
	[ObservableProperty] private IRootDock m_mainLayout;
	[ObservableProperty] private bool m_schemaEditorVisible;
	[ObservableProperty] private bool m_tableEditorVisible;

	[ObservableProperty] private bool m_toastZoneActive;
	[ObservableProperty] private double m_toastZoneHeight = 400;
	[ObservableProperty] private IRootDock m_toastZoneLayout;
	private bool m_toastZonePinned;

	public MainWindowViewModel(ToastEngine toast) {
		Current = this;
		m_toast = toast;
		VoxelEditor.VoxelEditorActions.Register();

		GitState = new GitViewModel(GitService.Start(ProjectContext.ProjectPath));
		GitState.PropertyChanged += (_, e) => {
			if (e.PropertyName != nameof(GitViewModel.IsAvailable)) return;
			LockArtworkCommand.NotifyCanExecuteChanged();
			OpenCommitCommand.NotifyCanExecuteChanged();
			PullCommand.NotifyCanExecuteChanged();
			PushCommand.NotifyCanExecuteChanged();
		};

		m_dockFactory = new DockFactory();
		MainLayout = m_dockFactory.CreateLayout();
		m_dockFactory.InitLayout(MainLayout);

		m_toastZoneFactory = new ToastZoneFactory();
		ToastZoneLayout = m_toastZoneFactory.CreateLayout();
		m_toastZoneFactory.InitLayout(ToastZoneLayout);

		m_defaultLayout = new LayoutFile {
			Name = LayoutStore.DefaultName,
			Main = m_dockFactory.CaptureLayout(),
			Toast = m_toastZoneFactory.CaptureLayout(),
			ToastZoneHeight = 400
		};

		m_dockFactory.DockableClosed += (_, e) => {
			if (e.Dockable is WorkspaceViewModel ws) m_workspaces.Remove(ws.Handle);
			if (e.Dockable == m_dockFactory.Hierarchy) m_hierarchyVisible = false;
			if (e.Dockable == m_dockFactory.History) m_historyVisible = false;
			if (e.Dockable == m_dockFactory.Locks) m_locksVisible = false;
			if (e.Dockable == m_dockFactory.Inspector) m_inspectorVisible = false;
			if (e.Dockable == m_dockFactory.Signals) m_signalsVisible = false;
			if (e.Dockable == m_dockFactory.GenericEditorVm) m_genericEditorVisible = false;
			if (e.Dockable == m_dockFactory.SchemaEditorVm) m_schemaEditorVisible = false;

			OnPropertyChanged(nameof(HierarchyVisible));
			OnPropertyChanged(nameof(HistoryVisible));
			OnPropertyChanged(nameof(LocksVisible));
			OnPropertyChanged(nameof(InspectorVisible));
			OnPropertyChanged(nameof(SignalsVisible));
			OnPropertyChanged(nameof(GenericEditorVisible));
			OnPropertyChanged(nameof(SchemaEditorVisible));

			if (m_workspaces.Count == 0) {
				m_activeWorkspaceHandle = 0;
				m_dockFactory.Hierarchy?.Clear();
				m_dockFactory.History?.Clear();
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
			if (e.Dockable == m_toastZoneFactory.TableEditorVm) m_tableEditorVisible = false;
			if (e.Dockable == m_toastZoneFactory.PaletteEditorVm) m_paletteEditorVisible = false;

			OnPropertyChanged(nameof(LogsVisible));
			OnPropertyChanged(nameof(HapticsEditorVisible));
			OnPropertyChanged(nameof(CurveEditorVisible));
			OnPropertyChanged(nameof(TableEditorVisible));
			OnPropertyChanged(nameof(PaletteEditorVisible));
		};

		m_dockFactory.ActiveDockableChanged += (_, _) => {
			SyncActiveWorkspace();
			if (m_dockFactory.Signals?.IsActive == true) m_dockFactory.Signals.Refresh();
			PlayCommand.NotifyCanExecuteChanged();
			SimulateCommand.NotifyCanExecuteChanged();
			PlayInWindowCommand.NotifyCanExecuteChanged();
		};

		m_dockFactory.SchemaEditorVm!.SchemaSaved += OnSchemaSaved;

		EditorManager.OpenRequested += OnEditorOpenRequested;

		WorkspaceViewModel.PlayModeChanged += OnPlayModeChanged;

		m_autosave = new AutosaveService(EnumerateAutosavables);
	}

	public HierarchyViewModel? Hierarchy => m_dockFactory.Hierarchy;
	public HistoryViewModel? History => m_dockFactory.History;

	public GitViewModel GitState { get; }

	public IReadOnlyList<string> LayoutNames => LayoutStore.EnumerateNames();

	public bool CanModifyActiveLayout => !LayoutStore.IsBuiltin(ActiveLayoutName);

	public void Dispose() {
		if (ReferenceEquals(Current, this)) Current = null;
		m_autosave.Stop();
		m_projectSettingsWindow?.Close();
		m_projectSettingsWindow = null;
		m_commitWindow?.Close();
		m_commitWindow = null;
		m_lockArtworkWindow?.Close();
		m_lockArtworkWindow = null;
		EditorManager.OpenRequested -= OnEditorOpenRequested;
		WorkspaceViewModel.PlayModeChanged -= OnPlayModeChanged;
		if (m_dockFactory.SchemaEditorVm is { } schema) schema.SchemaSaved -= OnSchemaSaved;
		foreach (var workspace in m_workspaces.Values) workspace.Dispose();
		m_dockFactory.Hierarchy?.Dispose();
		m_dockFactory.Inspector?.Dispose();
		m_dockFactory.Signals?.Dispose();
		m_dockFactory.History?.Dispose();
		m_dockFactory.Locks?.Dispose();
		m_toastZoneFactory.AssetBrowserVm?.Dispose();
		m_toastZoneFactory.TableEditorVm?.Dispose();
		GitState.Dispose();
		GitService.Current?.Dispose();
		GC.SuppressFinalize(this);
	}

	private void OnSchemaSaved(string path) {
		m_dockFactory.GenericEditorVm?.RefreshFromSchema(path);
		DataSchemaStubGenerator.Generate();
	}

	private void OnPlayModeChanged() {
		var active = m_dockFactory.ActiveWorkspace;
		m_activeWorkspaceHandle = active?.EffectiveHandle ?? 0;
		m_dockFactory.History?.SetWorkspace(active is { PlayHandle: 0 } ? active.History : null);

		SaveCurrentNodeCommand.NotifyCanExecuteChanged();
		SaveCurrentNodeAsCommand.NotifyCanExecuteChanged();
		SaveAllNodesCommand.NotifyCanExecuteChanged();
		PlayCommand.NotifyCanExecuteChanged();
		PlayInWindowCommand.NotifyCanExecuteChanged();
	}

	private IEnumerable<IAutosavable> EnumerateAutosavables() {
		foreach (var ws in m_workspaces.Values) yield return ws;
		if (m_dockFactory.GenericEditorVm is { } generic) yield return generic;
		if (m_dockFactory.SchemaEditorVm is { } schema) yield return schema;
		if (m_toastZoneFactory.CurveEditorVm is { } curve) yield return curve;
		if (m_toastZoneFactory.HapticsEditorVm is { } haptics) yield return haptics;
		if (m_toastZoneFactory.TableEditorVm is { } table) yield return table;
		if (m_toastZoneFactory.PaletteEditorVm is { } palette) yield return palette;
	}

	[RelayCommand]
	private Task AutosaveNow() {
		return m_autosave.RequestAutosave();
	}

	[RelayCommand]
	private static void OpenDocumentation() {
		Process.Start(new ProcessStartInfo { FileName = "https://docs.nullptr.es", UseShellExecute = true });
	}

	[RelayCommand]
	private void OpenProjectSettings() {
		if (m_projectSettingsWindow is { } existing) {
			if (existing.WindowState == WindowState.Minimized) existing.WindowState = WindowState.Normal;
			existing.Activate();
			return;
		}

		if (m_dockFactory.ProjectSettingsVm is not { } viewModel) return;

		var window = new ProjectSettingsWindow(viewModel);
		m_projectSettingsWindow = window;
		window.Closed += (_, _) => m_projectSettingsWindow = null;

		if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime {
			    MainWindow: { } owner
		    })
			window.Show(owner);
		else
			window.Show();
	}

	public void OpenProjectSettingsAt(SettingsTab tab, string? category = null) {
		OpenProjectSettings();
		m_dockFactory.ProjectSettingsVm?.SelectSection(tab, category);
	}

	partial void OnHierarchyVisibleChanged(bool value) {
		ToggleMainTool("Hierarchy", value);
	}

	partial void OnHistoryVisibleChanged(bool value) {
		ToggleMainTool("History", value);
	}

	partial void OnLocksVisibleChanged(bool value) {
		ToggleMainTool("Locks", value);
	}

	private bool CanUseGit() {
		return GitState.IsAvailable;
	}

	[RelayCommand(CanExecute = nameof(CanUseGit))]
	private void OpenCommit() {
		if (m_commitWindow is { } existing) {
			if (existing.WindowState == WindowState.Minimized) existing.WindowState = WindowState.Normal;
			existing.Activate();
			return;
		}

		var window = new CommitWindow(new CommitViewModel(GitState.Service));
		m_commitWindow = window;
		window.Closed += (_, _) => m_commitWindow = null;

		if (App.MainWindow is { } owner) window.Show(owner);
		else window.Show();
	}

	[RelayCommand(CanExecute = nameof(CanUseGit))]
	private Task Pull() {
		return RunGitOperation("Pull", GitState.Service.PullAsync, true);
	}

	[RelayCommand(CanExecute = nameof(CanUseGit))]
	private Task Push() {
		return RunGitOperation("Push", GitState.Service.PushAsync, false);
	}

	// Opens the picker for the lockable files in artwork://
	[RelayCommand(CanExecute = nameof(CanUseGit))]
	private async Task LockArtwork() {
		if (m_lockArtworkWindow is { } existing) {
			existing.Activate();
			return;
		}

		if (!GitState.Service.HasLfs) {
			await App.Modals.ShowError("Lock artwork", "git-lfs is not installed, so files can't be locked.");
			return;
		}

		var window = new LockArtworkWindow(new LockArtworkViewModel(GitState.Service));
		m_lockArtworkWindow = window;
		window.Closed += (_, _) => m_lockArtworkWindow = null;

		if (App.MainWindow is { } owner) window.Show(owner);
		else window.Show();
	}

	private async Task RunGitOperation(string label, Func<Task<GitResult>> operation, bool assetsMayChange) {
		if (m_gitBusy || App.MainWindow is not { } owner) return;
		m_gitBusy = true;
		try {
			var output = "";
			var task = LoaderTask.Do(label, async log => {
				var result = await operation();
				output = result.Message;
				foreach (var line in result.Message.Split('\n', StringSplitOptions.RemoveEmptyEntries)) log(line.TrimEnd());
				if (!result.Ok) throw new InvalidOperationException(DescribeFailure(label, result.Message));
			});

			var vm = new LoaderViewModel([task]) {
				OnComplete = async () => {
					// A pull that brought nothing new leaves the database alone
					if (assetsMayChange && !output.Contains("Already up to date", StringComparison.OrdinalIgnoreCase))
						AssetDatabase.RebuildAssetDatabase();
					await Task.CompletedTask;
				}
			};

			await new SimpleLoaderWindow(vm).ShowDialog(owner);
		} finally {
			m_gitBusy = false;
		}
	}

	private static string DescribeFailure(string label, string message) {
		if (label == "Pull" && message.Contains("fast-forward", StringComparison.OrdinalIgnoreCase))
			return "Your branch and the remote have diverged, so it can't be fast-forwarded.\n" +
			       "Push your commits or resolve this outside the editor.\n\n" + message;
		return message;
	}

	partial void OnInspectorVisibleChanged(bool value) {
		ToggleMainTool("Inspector", value);
	}

	partial void OnSignalsVisibleChanged(bool value) {
		ToggleMainTool("Signals", value);
	}

	partial void OnGenericEditorVisibleChanged(bool value) {
		ToggleMainTool("GenericEditor", value);
	}

	partial void OnSchemaEditorVisibleChanged(bool value) {
		ToggleMainTool("SchemaEditor", value);
	}

	partial void OnLogsVisibleChanged(bool value) {
		ToggleToastTool("Logs", value);
	}

	partial void OnHapticsEditorVisibleChanged(bool value) {
		ToggleToastTool("Haptics", value);
	}

	partial void OnCurveEditorVisibleChanged(bool value) {
		ToggleToastTool("Curve", value);
	}

	partial void OnTableEditorVisibleChanged(bool value) {
		ToggleToastTool("Table", value);
	}

	partial void OnPaletteEditorVisibleChanged(bool value) {
		ToggleToastTool("Palette", value);
	}

	private void ToggleMainTool(string id, bool value) {
		if (m_applyingLayout) return;
		if (value != m_dockFactory.IsToolVisible(id)) m_dockFactory.ToggleTool(id);
	}

	private void ToggleToastTool(string id, bool value) {
		if (m_applyingLayout) return;
		if (value != m_toastZoneFactory.IsToolVisible(id)) m_toastZoneFactory.ToggleTool(id);
	}

	private async void OnEditorOpenRequested(AssetFile file) {
		if (file.Definition is not { CanBeEdited: true } def) return;
		if (file.Uid is not { } uid) return;
		if (!AssetDatabase.TryResolve(uid, out var virtualPath, out _)) return;

		// Opening a lockable asset nobody holds takes its lock
		if (def is not ProjectSettingsAsset) await GitLockGuard.LockOnOpenAsync(ProjectContext.Resolve(virtualPath));

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
				await OpenWorkspaceFile(uid, virtualPath, recoverPath);
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
			case "TableEditor":
				if (m_toastZoneFactory.TableEditorVm is { } tableVm) {
					_ = OpenToastEditorAsync(tableVm, uid, virtualPath, def, recoverPath);
					TableEditorVisible = true;
				}

				break;
			case "PaletteEditor":
				if (m_toastZoneFactory.PaletteEditorVm is { } paletteVm) {
					_ = OpenToastEditorAsync(paletteVm, uid, virtualPath, def, recoverPath);
					PaletteEditorVisible = true;
				}

				break;
		}
	}

	private async Task OpenWorkspaceFile(string uid, string virtualPath, string? recoverPath) {
		var recoverVirtual = recoverPath is null ? null : ProjectContext.ToVirtual(recoverPath);

		LoadingPopup? popup = null;
		if (App.MainWindow is { } owner) {
			popup = new LoadingPopup($"Loading {Path.GetFileName(virtualPath)}");
			_ = popup.ShowDialog(owner);
			await Avalonia.Threading.Dispatcher.UIThread.InvokeAsync(() => { }, Avalonia.Threading.DispatcherPriority.Render);
		}

		try {
			var ws = await WorkspaceViewModel.OpenFileAsync(m_toast, uid, virtualPath, recoverVirtual);
			if (ws is null) return;
			m_workspaces[ws.Handle] = m_dockFactory.AddWorkspace(ws);
			SyncActiveWorkspace();
			if (popup is not null) await WaitForEngineAsync(ws);
		} finally {
			popup?.Close();
		}
	}

	private async Task WaitForEngineAsync(WorkspaceViewModel ws) {
		const int smoothFrames = 30;
		const long capMs = 90_000;
		const long responsiveMs = 50;
		const int responsiveRuns = 5;

		var timer = System.Diagnostics.Stopwatch.StartNew();

		// The engine answers SetActiveWorkspace with the hierarchy
		while (m_dockFactory.Hierarchy is { HasCurrentHierarchy: false } && timer.ElapsedMilliseconds < capMs)
			await Task.Delay(30);

		await Task.Run(() => {
			var responsive = 0;
			while (responsive < responsiveRuns && timer.ElapsedMilliseconds < capMs && ToastEngine.IsEngineReady) {
				var start = timer.ElapsedMilliseconds;
				ToastEngine.OnTickThread(() => { });
				responsive = timer.ElapsedMilliseconds - start <= responsiveMs ? responsive + 1 : 0;
				Thread.Sleep(20);
			}
		});

		while (ws.SmoothFrames < smoothFrames && timer.ElapsedMilliseconds < capMs && ToastEngine.IsEngineReady)
			await Task.Delay(30);
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

	public ToastEngine Engine => m_toast;

	public WorkspaceViewModel? FindOpenWorkspace(string assetUid) {
		return m_workspaces.Values.FirstOrDefault(w => w.BackingAssetUid == assetUid);
	}

	// if another window spawns we need to be able to give focus back to the game window
	public void ReclaimEngine() {
		if (ViewportFocus.DetachedOwner == 0) return;
		Events.Send(new SetVoxelEditorOverlays { UnitGrid = false, VoxelGrid = false, Edges = false, VoxelEdges = false });
		Events.Send(new SetShowOthers { Show = false });
		ViewportFocus.DetachedOwner = 0;
		m_dockFactory.Hierarchy?.MakeCurrent();
		m_activeWorkspaceHandle = ulong.MaxValue;
		SyncActiveWorkspace();
		if (m_dockFactory.ActiveWorkspace is { } workspace) workspace.ResendViewState();
		Events.Send(new RequestHierarchyUpdate());
	}

	private void SyncActiveWorkspace() {
		if (m_applyingLayout) return;

		// a playing tab routes to its temporary play clone, not the frozen editing workspace
		var workspace = m_dockFactory.ActiveWorkspace;
		var handle = workspace?.EffectiveHandle ?? 0;
		if (handle == m_activeWorkspaceHandle) return;
		m_activeWorkspaceHandle = handle;
		m_dockFactory.Hierarchy?.Clear();
		m_dockFactory.History?.SetWorkspace(workspace is { PlayHandle: 0 } ? workspace.History : null);
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

	private LayoutFile CaptureCurrent(string name) {
		return new LayoutFile {
			Name = name,
			Main = m_dockFactory.CaptureLayout(),
			Toast = m_toastZoneFactory.CaptureLayout(),
			ToastZoneHeight = ToastZoneHeight
		};
	}

	private void ApplyLayout(LayoutFile file) {
		if (WorkspaceViewModel.AnyPlayActive) return;

		var dirty = CollectDirtyTools();

		m_applyingLayout = true;
		try {
			var mainRoot = m_dockFactory.RebuildLayout(file.Main)
				?? m_dockFactory.RebuildLayout(m_defaultLayout.Main);
			var toastRoot = m_toastZoneFactory.RebuildLayout(file.Toast)
				?? m_toastZoneFactory.RebuildLayout(m_defaultLayout.Toast);
			if (mainRoot is null || toastRoot is null) {
				Log.Warn($"Layout '{file.Name}' describes no usable dock tree");
				return;
			}

			if (MainLayout.ExitWindows.CanExecute(null)) MainLayout.ExitWindows.Execute(null);
			if (ToastZoneLayout.ExitWindows.CanExecute(null)) ToastZoneLayout.ExitWindows.Execute(null);
			MainLayout.HiddenDockables?.Clear();
			ToastZoneLayout.HiddenDockables?.Clear();

			m_dockFactory.InitLayout(mainRoot);
			m_toastZoneFactory.InitLayout(toastRoot);

			MainLayout = mainRoot;
			ToastZoneLayout = toastRoot;

			SyncVisibilityFromDocks();
			ToastZoneHeight = Math.Clamp(file.ToastZoneHeight, 100, 4000);
		} catch (Exception e) {
			Log.Warn($"Failed to apply layout '{file.Name}': {e.Message}");
		} finally {
			m_applyingLayout = false;
		}

		foreach (var id in dirty) RestoreTool(id);

		SyncActiveWorkspace();
	}

	private List<string> CollectDirtyTools() {
		var dirty = new List<string>();
		if (m_dockFactory.GenericEditorVm is { IsDirty: true }) dirty.Add("GenericEditor");
		if (m_dockFactory.SchemaEditorVm is { IsDirty: true }) dirty.Add("SchemaEditor");
		if (m_toastZoneFactory.CurveEditorVm is { IsDirty: true }) dirty.Add("Curve");
		if (m_toastZoneFactory.HapticsEditorVm is { IsDirty: true }) dirty.Add("Haptics");
		if (m_toastZoneFactory.TableEditorVm is { IsDirty: true }) dirty.Add("Table");
		if (m_toastZoneFactory.PaletteEditorVm is { IsDirty: true }) dirty.Add("Palette");
		return dirty;
	}

	private List<UnsavedItem> CollectUnsavedItems() {
		var items = new List<UnsavedItem>();

		foreach (var ws in m_workspaces.Values.Where(ws => ws.IsModified))
			items.Add(new UnsavedItem("Node", ws.Title ?? "Unnamed Node",
				async () => await ws.Save() && !ws.IsModified,
				ws.DeleteAutosaves));

		void AddEditor(string kind, bool dirty, string path, string uid, IAsyncRelayCommand save, Func<bool> stillDirty) {
			if (!dirty || string.IsNullOrEmpty(path)) return;
			items.Add(new UnsavedItem(kind, Path.GetFileName(path),
				async () => {
					await save.ExecuteAsync(null);
					return !stillDirty();
				},
				() => AutosaveService.Delete(uid, AssetTypeRegistry.GetExtension(path))));
		}

		if (m_dockFactory.GenericEditorVm is { } generic)
			AddEditor("Data", generic.IsDirty, generic.CurrentPath, generic.CurrentUid, generic.SaveCommand,
				() => generic.IsDirty);
		if (m_dockFactory.SchemaEditorVm is { } schema)
			AddEditor("Schema", schema.IsDirty, schema.CurrentPath, schema.CurrentUid, schema.SaveCommand,
				() => schema.IsDirty);
		if (m_toastZoneFactory.CurveEditorVm is { } curve)
			AddEditor("Curve", curve.IsDirty, curve.CurrentPath, curve.CurrentUid, curve.SaveCommand, () => curve.IsDirty);
		if (m_toastZoneFactory.HapticsEditorVm is { } haptics)
			AddEditor("Haptics", haptics.IsDirty, haptics.CurrentPath, haptics.CurrentUid, haptics.SaveCommand,
				() => haptics.IsDirty);
		if (m_toastZoneFactory.TableEditorVm is { } table)
			AddEditor("Table", table.IsDirty, table.CurrentPath, table.CurrentUid, table.SaveCommand, () => table.IsDirty);
		if (m_toastZoneFactory.PaletteEditorVm is { } palette)
			AddEditor("Palette", palette.IsDirty, palette.CurrentPath, palette.CurrentUid, palette.SaveCommand,
				() => palette.IsDirty);

		return items;
	}

	public bool HasUnsavedWork() {
		return CollectUnsavedItems().Count > 0;
	}

	public async Task<bool> ConfirmCloseAllAsync(Avalonia.Controls.Window owner) {
		var items = CollectUnsavedItems();
		if (items.Count == 0) return true;

		switch (await new UnsavedChangesModal(items).ShowDialog<UnsavedChangesResult>(owner)) {
			case UnsavedChangesResult.SaveAll:
				// cancelling keeps the editor open
				foreach (var item in items)
					if (!await item.Save())
						return false;
				return true;
			case UnsavedChangesResult.DiscardAll:
				foreach (var item in items) item.Discard();
				return true;
			default:
				return false;
		}
	}

	private void RestoreTool(string id) {
		switch (id) {
			case "GenericEditor": GenericEditorVisible = true; break;
			case "SchemaEditor": SchemaEditorVisible = true; break;
			case "Curve": CurveEditorVisible = true; break;
			case "Haptics": HapticsEditorVisible = true; break;
			case "Table": TableEditorVisible = true; break;
			case "Palette": PaletteEditorVisible = true; break;
		}
	}

#pragma warning disable MVVMTK0034
	private void SyncVisibilityFromDocks() {
		m_hierarchyVisible = m_dockFactory.IsToolVisible("Hierarchy");
		m_historyVisible = m_dockFactory.IsToolVisible("History");
		m_locksVisible = m_dockFactory.IsToolVisible("Locks");
		m_inspectorVisible = m_dockFactory.IsToolVisible("Inspector");
		m_signalsVisible = m_dockFactory.IsToolVisible("Signals");
		m_genericEditorVisible = m_dockFactory.IsToolVisible("GenericEditor");
		m_schemaEditorVisible = m_dockFactory.IsToolVisible("SchemaEditor");
		m_logsVisible = m_toastZoneFactory.IsToolVisible("Logs");
		m_hapticsEditorVisible = m_toastZoneFactory.IsToolVisible("Haptics");
		m_curveEditorVisible = m_toastZoneFactory.IsToolVisible("Curve");
		m_tableEditorVisible = m_toastZoneFactory.IsToolVisible("Table");
		m_paletteEditorVisible = m_toastZoneFactory.IsToolVisible("Palette");

		OnPropertyChanged(nameof(HierarchyVisible));
		OnPropertyChanged(nameof(HistoryVisible));
		OnPropertyChanged(nameof(LocksVisible));
		OnPropertyChanged(nameof(InspectorVisible));
		OnPropertyChanged(nameof(SignalsVisible));
		OnPropertyChanged(nameof(GenericEditorVisible));
		OnPropertyChanged(nameof(SchemaEditorVisible));
		OnPropertyChanged(nameof(LogsVisible));
		OnPropertyChanged(nameof(HapticsEditorVisible));
		OnPropertyChanged(nameof(CurveEditorVisible));
		OnPropertyChanged(nameof(TableEditorVisible));
		OnPropertyChanged(nameof(PaletteEditorVisible));
	}
#pragma warning restore MVVMTK0034

	public void RestoreSession() {
		if (!ProjectContext.IsInitialized) return;
		if (LayoutStore.LoadSession() is not { } session) return;
		ApplyLayout(session);
		ActiveLayoutName = LayoutStore.IsBuiltin(session.Name) ? LayoutStore.DefaultName : session.Name;
	}

	public void SaveSessionLayout() {
		try {
			LayoutStore.SaveSession(CaptureCurrent(ActiveLayoutName));
		} catch (Exception e) {
			Log.Warn($"Failed to save the session layout: {e.Message}");
		}
	}

	[RelayCommand]
	private void ApplyNamedLayout(string? name) {
		if (string.IsNullOrEmpty(name)) return;
		var file = LayoutStore.IsBuiltin(name) ? m_defaultLayout : LayoutStore.Load(name);
		if (file is null) return;
		ApplyLayout(file);
		ActiveLayoutName = LayoutStore.IsBuiltin(name) ? LayoutStore.DefaultName : name;
	}

	[RelayCommand]
	private void ResetLayout() {
		ApplyLayout(m_defaultLayout);
		ActiveLayoutName = LayoutStore.DefaultName;
	}

	[RelayCommand]
	private void SaveLayout() {
		if (!CanModifyActiveLayout) return;
		LayoutStore.Save(ActiveLayoutName, CaptureCurrent(ActiveLayoutName));
	}

	[RelayCommand]
	private async Task SaveLayoutAs() {
		if (App.MainWindow is not { } owner) return;

		var name = await new RenameModal("", "Save Layout", "Save", LucideIconKind.Save, "Layout name...")
			.ShowDialog<string?>(owner);
		if (string.IsNullOrWhiteSpace(name)) return;

		if (LayoutStore.IsBuiltin(name)) {
			await App.Modals.ShowWarning("Save Layout", $"\"{LayoutStore.DefaultName}\" is reserved.");
			return;
		}

		if (!LayoutStore.IsValidName(name)) {
			await App.Modals.ShowWarning("Save Layout", "That name contains invalid characters.");
			return;
		}

		if (LayoutStore.Exists(name) && !await App.Modals.ShowConfirm("Save Layout", $"Overwrite \"{name}\"?"))
			return;

		LayoutStore.Save(name, CaptureCurrent(name));
		ActiveLayoutName = name;
	}

	[RelayCommand]
	private async Task DeleteLayout(string? name) {
		if (string.IsNullOrEmpty(name) || LayoutStore.IsBuiltin(name)) return;
		if (App.MainWindow is not { } owner) return;

		var confirmed = await new MessageModal(new ModalConfig(
			"Delete Layout",
			$"Delete the layout \"{name}\"? This cannot be undone.",
			ModalButtons.OkCancel,
			LucideIconKind.Shredder,
			new SolidColorBrush(Color.Parse("#d04040")),
			"Delete",
			OkIcon: LucideIconKind.Shredder
		)).ShowDialog<bool?>(owner) == true;
		if (!confirmed) return;

		LayoutStore.Delete(name);
		if (string.Equals(ActiveLayoutName, name, StringComparison.Ordinal)) ResetLayout();
	}

	[RelayCommand]
	private async Task NewNode() {
		if (App.MainWindow is not { } owner) return;
		var popup = new NodeTypeTree();
		var result = await popup.ShowDialog<string?>(owner);
		if (result is null) return;

		if (WorkspaceViewModel.CreateNew(m_toast, result) is not { } ws) return;
		m_workspaces[ws.Handle] = m_dockFactory.AddWorkspace(ws);
		SyncActiveWorkspace();
	}

	[RelayCommand]
	private async Task OpenNodeFile() {
		if (App.MainWindow is not { } owner) return;
		var uid = await new AssetList("Node").ShowDialog<string?>(owner);
		if (uid is null) return;

		if (!AssetDatabase.TryResolve(uid, out var virtualPath, out _)) return;

		await GitLockGuard.LockOnOpenAsync(ProjectContext.Resolve(virtualPath));
		var recoverPath = await AutosaveService.TryRecoverAsync(uid, virtualPath);
		await OpenWorkspaceFile(uid, virtualPath, recoverPath);
	}

	public void ReopenWorkspaceFromDisk(WorkspaceViewModel workspace) {
		var uid = workspace.BackingAssetUid;
		var virtualPath = workspace.BackingUri;
		if (uid is null || virtualPath is null) return;

		Avalonia.Threading.Dispatcher.UIThread.Post(() => {
			workspace.PendingClose = true; // skip the unsaved changes prompt
			m_dockFactory.CloseDockable(workspace);
			OpenWorkspaceFile(uid, virtualPath, null);
		});
	}

	[RelayCommand]
	private void CloseNodeFile() {
		if (m_dockFactory.ActiveWorkspace is { } ws) m_dockFactory.CloseDockable(ws);
	}

	/// <summary>
	/// Deletes baked probe and irradiance files whose node no longer exists in the project
	/// </summary>
	/// <remarks>
	/// Manual and confirmed, never automatic: this deletes minutes of baking, and the scan only sees scenes
	/// under the project's databases - a level stored elsewhere would lose its lighting silently
	/// </remarks>
	[RelayCommand]
	private async Task CleanBakedLightingCache() {
		if (App.MainWindow is not { } owner) return;

		if (!ProjectContext.IsInitialized) {
			await new MessageModal(new ModalConfig(
				"Clean Baked Lighting",
				"No project is open.",
				Icon: LucideIconKind.Info
			)).ShowDialog<bool?>(owner);
			return;
		}

		var orphans = await Task.Run(BakedLightingCache.FindOrphans);
		if (orphans.Count == 0) {
			await new MessageModal(new ModalConfig(
				"Clean Baked Lighting",
				"No orphaned bakes found - every cached probe and volume still belongs to a node in this project.",
				Icon: LucideIconKind.Check
			)).ShowDialog<bool?>(owner);
			return;
		}

		var total = orphans.Sum(o => o.Bytes);
		var probes = orphans.Count(o => o.Kind == "Reflection probe");
		var volumes = orphans.Count - probes;

		var breakdown = string.Join(", ", new[] {
			probes > 0 ? $"{probes} reflection probe{(probes == 1 ? "" : "s")}" : null,
			volumes > 0 ? $"{volumes} irradiance volume{(volumes == 1 ? "" : "s")}" : null
		}.Where(s => s is not null));

		var confirmed = await new MessageModal(new ModalConfig(
			"Clean Baked Lighting",
			$"Found {breakdown} with no node in this project, using {BakedLightingCache.FormatSize(total)}.\n\n" +
			"Delete them? Any node that is restored later will have to be re-baked.",
			ModalButtons.OkCancel,
			LucideIconKind.Shredder,
			new SolidColorBrush(Color.Parse("#d04040")),
			"Delete",
			OkIcon: LucideIconKind.Shredder
		)).ShowDialog<bool?>(owner) == true;
		if (!confirmed) return;

		var (deleted, bytes, failed) = BakedLightingCache.Delete(orphans);

		var message = $"Deleted {deleted} file{(deleted == 1 ? "" : "s")}, freeing {BakedLightingCache.FormatSize(bytes)}.";
		if (failed.Count > 0) message += $"\n\n{failed.Count} could not be removed: {string.Join(", ", failed.Take(5))}";

		await new MessageModal(new ModalConfig(
			"Clean Baked Lighting",
			message,
			Icon: failed.Count > 0 ? LucideIconKind.TriangleAlert : LucideIconKind.Check
		)).ShowDialog<bool?>(owner);
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

	private static bool CanReloadGame() {
		return ProjectContext.IsInitialized;
	}

	private static bool CanCompileGameRelease() {
		return ProjectContext.IsInitialized;
	}

	private bool CanPlay() {
		return m_dockFactory.ActiveWorkspace is { } ws && ws.TogglePlayCommand.CanExecute(null);
	}

	private bool CanSimulate() {
		return m_dockFactory.ActiveWorkspace is { } ws && ws.ToggleSimulateCommand.CanExecute(null);
	}

	private bool CanPlayInWindow() {
		return m_dockFactory.ActiveWorkspace is { } ws && ws.TogglePlayExternalCommand.CanExecute(null);
	}

	[RelayCommand(CanExecute = nameof(CanPlay))]
	private void Play() {
		if (m_dockFactory.ActiveWorkspace is { } ws)
			ws.TogglePlayCommand.Execute(null);
	}

	[RelayCommand(CanExecute = nameof(CanSimulate))]
	private void Simulate() {
		if (m_dockFactory.ActiveWorkspace is { } ws)
			ws.ToggleSimulateCommand.Execute(null);
	}

	[RelayCommand(CanExecute = nameof(CanPlayInWindow))]
	private void PlayInWindow() {
		if (m_dockFactory.ActiveWorkspace is { } ws)
			ws.TogglePlayExternalCommand.Execute(null);
	}

	[RelayCommand(CanExecute = nameof(CanReloadGame))]
	private async Task ReloadGame() {
		if (App.MainWindow is not { } owner) return;
		if (!ProjectContext.IsInitialized) return;

		var toastPath = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "toast_engine"));
		var cmakeGenerator = OperatingSystem.IsWindows() ? "-G \"Visual Studio 18 2026\"" : "-G \"Ninja\"";

		var tasks = new List<LoaderTask>();

		// Generate game reflection data before cmake
		var libSrc = Path.Combine(ProjectContext.ProjectPath, "lib", "src");
		var libGenerated = Path.Combine(ProjectContext.ProjectPath, "lib", "generated");
		Directory.CreateDirectory(libGenerated);

		var exeExt = OperatingSystem.IsWindows() ? ".exe" : "";
		var refgen = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,
			"..", "reflection_generator", $"reflection_generator{exeExt}"));
		var gameDb = ProjectContext.Resolve("cache://game_reflect.json");
		var gameLuaStubs = ProjectContext.Resolve("cache://lua/game_types.d.lua");
		var gameEventLuaStubs = ProjectContext.Resolve("cache://lua/game_events.d.lua");
		tasks.Add(LoaderTask.Run("Generate game reflection", refgen,
			$"--database \"{gameDb}\" --output \"{libGenerated}\" --input \"{libSrc}\" " +
			$"--include-root \"{libSrc}\" --register-fn registerGameTypes --attribute Game " +
			$"--lua-stubs \"{gameLuaStubs}\" --event-lua-stubs \"{gameEventLuaStubs}\""));

		tasks.Add(LoaderTask.Do("Copy engine reflection", async log => {
			var src = Path.Combine(ProjectContext.CorePath, "engine_reflect.json");
			var dst = ProjectContext.Resolve("cache://engine_reflect.json");
			if (File.Exists(src)) {
				File.Copy(src, dst, true);
				log("Copied engine_reflect.json");
			}

			await Task.CompletedTask;
		}));

		tasks.Add(LoaderTask.Do("Sync lua definitions", async log => {
			ProjectContext.SyncLuaDefinitions(log);
			await Task.CompletedTask;
		}));

		tasks.Add(LoaderTask.Run(
			"cmake configure",
			"cmake",
			$"lib/ -B .toast/cmake_cache {cmakeGenerator} -DTOAST_PATH={toastPath}"
		));
		tasks.Add(LoaderTask.Run(
			"cmake build",
			"cmake",
			// the game shares std types with the engine so both must use the same CRT
#if DEBUG
			"--build .toast/cmake_cache --config Debug --parallel"
#else
			"--build .toast/cmake_cache --config Release --parallel"
#endif
		));

		tasks.Add(LoaderTask.Do("Reload game", async log => {
			m_toast.ReloadGame();
			await Task.CompletedTask;
		}));

		var vm = new LoaderViewModel(tasks) {
			OnComplete = async () => {
				ReflectionDatabase.Update();
				AssetDatabase.RebuildAssetDatabase();
				if (HierarchyViewModel.Current is { } hvm) hvm.SelectedNode = null;
				await Task.CompletedTask;
			}
		};

		await new SimpleLoaderWindow(vm).ShowDialog(owner);
	}

	// The release build packs the files on disk, so unsaved edits (e.g. init_scene in the project settings) would silently be
	// left out of player.exe; returns false when the user cancels
	private async Task<bool> SaveBeforeBuild(Avalonia.Controls.Window owner) {
		var dirtyWorkspaces = m_workspaces.Values.Where(ws => ws.IsModified).ToList();
		if (dirtyWorkspaces.Count == 0 && CollectDirtyTools().Count == 0) return true;

		var result = await new MessageModal(new ModalConfig(
			"Unsaved Changes",
			"The build packs the files on disk, so unsaved changes won't be included. Save everything before building?",
			ModalButtons.OkNoCancel,
			OkLabel: "Save All"
		)).ShowDialog<bool?>(owner);
		if (result is null) return false;
		if (result is false) return true;

		foreach (var ws in dirtyWorkspaces)
			if (!await ws.Save()) return false;
		if (m_dockFactory.GenericEditorVm is { IsDirty: true } generic) await generic.SaveCommand.ExecuteAsync(null);
		if (m_dockFactory.SchemaEditorVm is { IsDirty: true } schema) await schema.SaveCommand.ExecuteAsync(null);
		if (m_toastZoneFactory.CurveEditorVm is { IsDirty: true } curve) await curve.SaveCommand.ExecuteAsync(null);
		if (m_toastZoneFactory.HapticsEditorVm is { IsDirty: true } haptics) await haptics.SaveCommand.ExecuteAsync(null);
		if (m_toastZoneFactory.TableEditorVm is { IsDirty: true } table) await table.SaveCommand.ExecuteAsync(null);
		return true;
	}

	[RelayCommand(CanExecute = nameof(CanCompileGameRelease))]
	private async Task CompileGameRelease() {
		if (App.MainWindow is not { } owner) return;
		if (!ProjectContext.IsInitialized) return;
		if (!await SaveBeforeBuild(owner)) return;

		var playerPath =
			Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", "..", "tools", "player"));
		var toastPath = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "toast_engine"));
		var packerBin = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "packer",
			$"packer{(OperatingSystem.IsWindows() ? ".exe" : "")}"));
		var outputDir = Path.GetFullPath(Path.Combine(ProjectContext.ProjectPath, "build"));
		var stageRoot = Path.GetFullPath(Path.Combine(ProjectContext.ProjectPath, ".toast", "pack_stage"));
		var cmakeGenerator = OperatingSystem.IsWindows() ? "-G \"Visual Studio 18 2026\"" : "-G \"Ninja\"";

		Directory.CreateDirectory(outputDir);

		async Task CopyDlls(Action<string> log) {
			var libExt = OperatingSystem.IsWindows() ? ".dll" : ".so";
			var exeExt = OperatingSystem.IsWindows() ? ".exe" : "";
			var extensions = new[] { libExt, exeExt };
			var sources = new[] {
				Path.Combine(toastPath, "bin"),
				Path.Combine(toastPath, "lib")
			};
			foreach (var src in sources) {
				if (!Directory.Exists(src)) {
					log($"warning: source dir not found: {src}");
					continue;
				}

				var files = Directory.EnumerateFiles(src, "*.*", SearchOption.TopDirectoryOnly)
					.Where(f => extensions.Contains(Path.GetExtension(f), StringComparer.OrdinalIgnoreCase));
				foreach (var file in files) {
					var dest = Path.Combine(outputDir, Path.GetFileName(file));
					log($"  copy {Path.GetFileName(file)}");
					await Task.Run(() => File.Copy(file, dest, true));
				}
			}
		}

		async Task CopyProjectToast(Action<string> log) {
			var toastFile = Directory.EnumerateFiles(ProjectContext.ProjectPath, "*.toast").FirstOrDefault();
			if (toastFile is null) {
				log("warning: no .toast file found in project root");
				return;
			}

			var dest = Path.Combine(outputDir, Path.GetFileName(toastFile));
			log($"  copy {Path.GetFileName(toastFile)}");
			await Task.Run(() => File.Copy(toastFile, dest, true));
		}

		// Baked lighting (irradiance volume SH, reflection probe captures) lives in cache://, which belongs to no content
		// database and so never reaches a pak; the player resolves cache:// to <build>/cache
		async Task CopyBakedLighting(Action<string> log) {
			foreach (var dir in new[] { "irradiance", "probes" }) {
				var src = Path.Combine(ProjectContext.CachePath, dir);
				if (!Directory.Exists(src)) continue;

				var dest = Path.Combine(outputDir, "cache", dir);
				Directory.CreateDirectory(dest);
				foreach (var file in Directory.EnumerateFiles(src)) {
					log($"  copy cache/{dir}/{Path.GetFileName(file)}");
					await Task.Run(() => File.Copy(file, Path.Combine(dest, Path.GetFileName(file)), true));
				}
			}
		}

		async Task BakeAndPack(Action<string> log, string dbName, string dbSourceDir, string manifestJsonPath) {
			var stageDir = Path.Combine(stageRoot, dbName);
			if (Directory.Exists(stageDir)) Directory.Delete(stageDir, true);
			Directory.CreateDirectory(stageDir);

			// Copy all source files into staging
			log($"  staging {dbName}/ ...");
			await Task.Run(() => {
				foreach (var srcFile in Directory.EnumerateFiles(dbSourceDir, "*", SearchOption.AllDirectories)) {
					var rel = Path.GetRelativePath(dbSourceDir, srcFile);
					var dest = Path.Combine(stageDir, rel);
					Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
					File.Copy(srcFile, dest, true);
				}
			});

			// Read the manifest JSON to find all node UIDs, then overwrite their staged files with binary
			log($"  baking nodes in {dbName}/ ...");
			if (File.Exists(manifestJsonPath)) {
				var manifestJson = JsonNode.Parse(File.ReadAllText(manifestJsonPath));
				if (manifestJson?["node"] is JsonObject nodeCollection)
					foreach (var (uid, pathNode) in nodeCollection) {
						var virtualPath = pathNode?.GetValue<string>();
						if (virtualPath is null) continue;
						var sep = virtualPath.IndexOf("://", StringComparison.Ordinal);
						if (sep < 0) continue;
						var rel = virtualPath[(sep + 3)..];
						var dest = Path.Combine(stageDir, rel.Replace('/', Path.DirectorySeparatorChar));
						Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
						log($"    bake {rel}");
						await Task.Run(() => ToastEngine.BakeAsset(uid, dest));
					}
			}

			// Copy the manifest JSON into the stage folder root
			var manifestDest = Path.Combine(stageDir, $"{dbName}.json");
			if (File.Exists(manifestJsonPath)) {
				log($"  copying manifest {dbName}.json");
				await Task.Run(() => File.Copy(manifestJsonPath, manifestDest, true));
			} else {
				log($"  warning: manifest not found at {manifestJsonPath}");
			}

			// Run the Rust packer on the staging directory
			var pakOut = Path.Combine(outputDir, $"{dbName}.pak");
			log($"  packing → {dbName}.pak");
			await Task.Run(() => {
				using var proc = new Process();
				proc.StartInfo = new ProcessStartInfo {
					FileName = packerBin,
					Arguments = $"\"{stageDir}\" \"{pakOut}\"",
					WorkingDirectory = ProjectContext.ProjectPath,
					RedirectStandardOutput = true,
					RedirectStandardError = true,
					UseShellExecute = false,
					CreateNoWindow = true
				};
				proc.OutputDataReceived += (_, e) => {
					if (e.Data is not null) log(e.Data);
				};
				proc.ErrorDataReceived += (_, e) => {
					if (e.Data is not null) log(e.Data);
				};
				proc.Start();
				proc.BeginOutputReadLine();
				proc.BeginErrorReadLine();
				proc.WaitForExit();
				if (proc.ExitCode != 0)
					throw new Exception($"packer exited with code {proc.ExitCode} for {dbName}");
			});
		}

		AssetDatabase.RebuildAssetDatabase();

		var tasks = new List<LoaderTask> {
			// Publish player
			LoaderTask.Run(
				"dotnet publish player",
				"dotnet",
				$"publish \"{playerPath}\" -c Release -p:PublishSingleFile=true -p:OptimizationPreference=Speed -o \"{outputDir}\""
			),
			// Generate CMake on Release
			LoaderTask.Run(
				"cmake configure (Release)",
				"cmake",
				$"lib/ -B .toast/cmake_release_cache {cmakeGenerator} -DTOAST_PATH={toastPath} -DCMAKE_BUILD_TYPE=Release"
			),
			// Build Game DLL on Release
			LoaderTask.Run(
				"cmake build (Release)",
				"cmake",
				"--build .toast/cmake_release_cache --config Release --parallel"
			),
			// Copy Engine libs
			LoaderTask.Do("copy libraries", CopyDlls),
			LoaderTask.Do("copy project.toast", CopyProjectToast),
			LoaderTask.Do("copy baked lighting", CopyBakedLighting)
		};

		// Bake assets
		foreach (var db in ProjectContext.Databases) {
			var dbSource = Path.Combine(ProjectContext.ProjectPath, db);
			var manifestJs = Path.Combine(ProjectContext.CachePath, $"{db}.json");
			var dbCapture = db;
			tasks.Add(LoaderTask.Do($"bake & pack {db}://", log => BakeAndPack(log, dbCapture, dbSource, manifestJs)));
		}

		// Bake core
		var coreManifest = Path.Combine(ProjectContext.CachePath, "core.json");
		tasks.Add(LoaderTask.Do("bake & pack core://",
			log => BakeAndPack(log, "core", ProjectContext.CorePath, coreManifest)));

		var vm = new LoaderViewModel(tasks);
		await new SimpleLoaderWindow(vm).ShowDialog(owner);
	}
}
