using System;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public enum GizmoTool { Select, Translate, Rotate, Scale, Ruler }

public enum PlayState { Stopped, Playing, PlayingExternal }

public partial class WorkspaceViewModel : Document, IAutosavable {
	private static int s_playingCount;

	private bool m_countedPlaying;
	[ObservableProperty] private bool m_gameCamera;
	[ObservableProperty] private bool m_isPaused;
	private string? m_pendingRootName;

	[ObservableProperty] private PlayState m_playState;

	private PlayWindow? m_playWindow;
	[ObservableProperty] private double m_rotateSnap = 30;
	[ObservableProperty] private bool m_rotateSnapEnabled = true;
	[ObservableProperty] private double m_scaleSnap = 0.10;
	[ObservableProperty] private bool m_scaleSnapEnabled = true;
	[ObservableProperty] private double m_translateSnap = 0.10;

	[ObservableProperty] private bool m_translateSnapEnabled = true;

	private WorkspaceViewModel(ToastEngine? engine = null) {
		Engine = engine;
		Title = "Unnamed Node";
		CanDrag = false;
	}

	public ToastEngine? Engine { get; }

	public ulong Handle { get; init; }

	// set after user confirms close so DockFactory doesnt re-enter the dialog on the second call
	public bool PendingClose { get; set; }

	// null until the user saves -> Save() redirects to SaveAs() when its null
	public string? BackingUri { get; private set; }
	public string? BackingAssetUid { get; private set; }

	public string? RootUid { get; private set; }
	public GizmoTool ActiveTool { get; private set; } = GizmoTool.Select;

	public bool WorldSpace { get; private set; }

	public static bool AnyPlayActive { get; private set; }

	public ulong PlayHandle { get; private set; }

	public ulong EffectiveHandle => PlayHandle != 0 ? PlayHandle : Handle;

	public bool IsPlaying => PlayState == PlayState.Playing;
	public bool IsPlayingExternal => PlayState == PlayState.PlayingExternal;
	public bool IsPlayModeActive => PlayState != PlayState.Stopped;
	public bool CanPause => PlayState != PlayState.Stopped;

	public bool IsAutosaveDirty => IsModified;

	// saved workspaces autosave under the asset uid
	// never-saved ones fall back to the session root node uid so the data still survives a crash
	public string? AutosaveFileName => RootUid is null ? null : (BackingAssetUid ?? RootUid) + ".tnode";

	public Task WriteAutosaveAsync(string virtualPath) {
		Events.Send(new WorkspaceAutosave { Handle = Handle, Path = virtualPath });
		return Task.CompletedTask;
	}

	// called by HierarchyViewModel whenever the hierarchy tree updates
	public void SetRootNode(string uid) {
		RootUid = uid;
		if (m_pendingRootName is { } name) {
			m_pendingRootName = null;
			Events.Send(new NodeChangeName { Node = uid, Name = name });
		}
	}

	public void BindBackingFile(string virtualUri, string assetUid) {
		BackingUri = virtualUri;
		BackingAssetUid = assetUid;
	}

	public override bool OnClose() {
		StopPlay();
		Events.Send(new SetFocusedNode { Node = "" });
		Events.Send(new WorkspaceDestroy { Handle = Handle });
		return base.OnClose();
	}

	[RelayCommand]
	private void SetTool(string tool) {
		ActiveTool = Enum.Parse<GizmoTool>(tool);
		// always raise so re-clicking the checked toggle re-asserts its visual state
		OnPropertyChanged(nameof(ActiveTool));
		Events.Send(new SetGizmoTool { Tool = (uint)ActiveTool });
	}

	[RelayCommand]
	private void SetSpace(string space) {
		WorldSpace = space == "World";
		OnPropertyChanged(nameof(WorldSpace));
		Events.Send(new SetCoordinateSpace { World = WorldSpace });
	}

	[RelayCommand]
	private void SetTranslateSnap(string value) {
		TranslateSnap = double.Parse(value, CultureInfo.InvariantCulture);
	}

	[RelayCommand]
	private void SetRotateSnap(string value) {
		RotateSnap = double.Parse(value, CultureInfo.InvariantCulture);
	}

	[RelayCommand]
	private void SetScaleSnap(string value) {
		ScaleSnap = double.Parse(value, CultureInfo.InvariantCulture);
	}

	partial void OnTranslateSnapEnabledChanged(bool value) {
		SendSnapping(0, value, m_translateSnap);
	}

	partial void OnRotateSnapEnabledChanged(bool value) {
		SendSnapping(1, value, m_rotateSnap);
	}

	partial void OnScaleSnapEnabledChanged(bool value) {
		SendSnapping(2, value, m_scaleSnap);
	}

	partial void OnTranslateSnapChanged(double value) {
		SendSnapping(0, m_translateSnapEnabled, value);
	}

	partial void OnRotateSnapChanged(double value) {
		SendSnapping(1, m_rotateSnapEnabled, value);
	}

	partial void OnScaleSnapChanged(double value) {
		SendSnapping(2, m_scaleSnapEnabled, value);
	}

	private static void SendSnapping(uint kind, bool enabled, double value) {
		Events.Send(new SetSnapping { Kind = kind, Enabled = enabled, Value = (float)value });
	}

	partial void OnGameCameraChanged(bool value) {
		Events.Send(new SetCameraMode { Game = value });
	}

	public static event Action? PlayModeChanged;

	partial void OnPlayStateChanged(PlayState value) {
		OnPropertyChanged(nameof(IsPlaying));
		OnPropertyChanged(nameof(IsPlayingExternal));
		OnPropertyChanged(nameof(IsPlayModeActive));
		OnPropertyChanged(nameof(CanPause));
		TogglePlayCommand.NotifyCanExecuteChanged();
		TogglePlayExternalCommand.NotifyCanExecuteChanged();

		var playing = value != PlayState.Stopped;
		if (playing == m_countedPlaying) return;
		m_countedPlaying = playing;
		s_playingCount += playing ? 1 : -1;
		AnyPlayActive = s_playingCount > 0;
		PlayModeChanged?.Invoke();
	}

	partial void OnIsPausedChanged(bool value) {
		if (PlayHandle != 0) Events.Send(new WorkspacePause { Handle = PlayHandle, Paused = value });
		TogglePlayCommand.NotifyCanExecuteChanged();
		TogglePlayExternalCommand.NotifyCanExecuteChanged();
	}

	private bool CanTogglePlay() {
		return !IsPaused && PlayState != PlayState.PlayingExternal;
	}

	private bool CanTogglePlayExternal() {
		return !IsPaused && PlayState != PlayState.Playing;
	}

	[RelayCommand(CanExecute = nameof(CanTogglePlay))]
	private async Task TogglePlay() {
		if (PlayState == PlayState.Stopped) await StartPlay(false);
		else StopPlay();
	}

	[RelayCommand(CanExecute = nameof(CanTogglePlayExternal))]
	private async Task TogglePlayExternal() {
		if (PlayState == PlayState.Stopped) await StartPlay(true);
		else StopPlay();
	}

	private async Task StartPlay(bool external) {
		if (Engine is null) return;

		// keep an autosave before going into game mode
		if (AutosaveFileName is { } name) {
			var virtualPath = AutosaveService.VirtualPath(name);
			Directory.CreateDirectory(Path.GetDirectoryName(ProjectContext.Resolve(virtualPath))!);
			await WriteAutosaveAsync(virtualPath);
		}

		var res = Engine.PlayWorkspace(Handle);
		if (res.Uid == 0) return;
		PlayHandle = res.Uid;
		OnPropertyChanged(nameof(EffectiveHandle));

		// clear focus while this workspace is still the active one
		Events.Send(new SetFocusedNode { Node = "" });

		// hierarchy and inspector follow the active workspace
		Events.Send(new SetActiveWorkspace { Handle = PlayHandle });
		GameCamera = true;
		PlayState = external ? PlayState.PlayingExternal : PlayState.Playing;

		if (external) {
			m_playWindow = new PlayWindow { DataContext = this };
			m_playWindow.Closed += OnPlayWindowClosed;
			m_playWindow.Show();
		}
	}

	public void StopPlay() {
		if (PlayState == PlayState.Stopped) return;

		if (m_playWindow is { } window) {
			m_playWindow = null;
			window.Closed -= OnPlayWindowClosed;
			window.Close();
		}

		Events.Send(new SetFocusedNode { Node = "" });
		Events.Send(new WorkspaceDestroy { Handle = PlayHandle });
		PlayHandle = 0;
		OnPropertyChanged(nameof(EffectiveHandle));
		IsPaused = false;
		PlayState = PlayState.Stopped;
		GameCamera = false;

		// the source workspace was never closed
		Events.Send(new SetActiveWorkspace { Handle = Handle });
	}

	// closing the play window by hand behaves like pressing stop
	private void OnPlayWindowClosed(object? sender, EventArgs e) {
		m_playWindow = null;
		StopPlay();
	}

	// shows the save-changes dialog and handles save/discard/cancel
	// returns true if the caller should proceed with closing
	public async Task<bool> ConfirmCloseAsync() {
		if (!IsModified) return true;

		var result = await App.Modals.ShowSaveChanges(Title ?? "Unnamed Node");

		switch (result) {
			case SaveChangesResult.Cancel:
				return false;
			case SaveChangesResult.Save:
				await Save();
				return true;
			default:
				DeleteAutosaves(); // discarded changes -> the autosave is unwanted too
				return true;
		}
	}

	public async Task Save() {
		if (RootUid is null) return;

		if (BackingUri is null) {
			await SaveAs(); // no path yet -> prompt the user
			return;
		}

		Events.Send(new NodeChangeName { Node = RootUid, Name = Path.GetFileNameWithoutExtension(BackingUri) });
		Events.Send(new WorkspaceSave { Target = RootUid, Path = BackingUri });
		MetaFile.Touch(BackingUri);
		DeleteAutosaves();
		IsModified = false;
	}

	public async Task SaveAs() {
		if (RootUid is null) return;

		var virtualPath = await App.Modals.ShowSaveFile(Title ?? "Unnamed Node");
		if (virtualPath is null) return;

		var realPath = ProjectContext.Resolve(virtualPath);

		Directory.CreateDirectory(Path.GetDirectoryName(realPath)!);
		// engine needs the file to exist on disk before it will write to it
		File.WriteAllBytes(realPath, Array.Empty<byte>());

		var uid = UidGenerator.Generate();
		MetaFile.Write(realPath,
			new MetaHeader { Uid = uid, Type = AssetTypeRegistry.ByExtension(".tnode")?.Type ?? "node" });
		AssetDatabase.RebuildAssetDatabase();

		Events.Send(new ReloadAssetsManifest());
		Events.Send(new NodeChangeName { Node = RootUid, Name = Path.GetFileNameWithoutExtension(virtualPath) });
		Events.Send(new WorkspaceSave { Target = RootUid, Path = virtualPath });

		BackingUri = virtualPath;
		BackingAssetUid = uid;
		DeleteAutosaves();
		IsModified = false;
		ProjectContext.RaiseAssetsChanged();
	}

	// the asset-uid autosave and the root-uid one a never-saved workspace may have left
	private void DeleteAutosaves() {
		AutosaveService.Delete(BackingAssetUid, ".tnode");
		AutosaveService.Delete(RootUid, ".tnode");
	}

	// creates a new empty workspace of the given node type
	public static WorkspaceViewModel? CreateNew(ToastEngine engine, string nodeType) {
		var res = engine.CreateWorkspace(nodeType);
		if (res.Uid == 0) return null;
		return new WorkspaceViewModel(engine) {
			Handle = res.Uid,
			Title = Marshal.PtrToStringUTF8(res.Name) ?? "Unnamed Node",
			Id = $"Workspace_{res.Uid}"
		};
	}

	// opens an existing node file by asset UID (engine deserializes it on its side)
	// recoverVirtualPath makes the engine load the content from an autosave
	public static WorkspaceViewModel? OpenFile(
		ToastEngine engine, string assetUid, string virtualPath, string? recoverVirtualPath = null) {
		var res = recoverVirtualPath is null
			? engine.OpenWorkspace(assetUid)
			: engine.OpenWorkspaceFrom(assetUid, recoverVirtualPath);
		if (res.Uid == 0) return null;
		var ws = new WorkspaceViewModel(engine) {
			Handle = res.Uid,
			Title = Marshal.PtrToStringUTF8(res.Name) ?? "Unnamed Node",
			Id = $"Workspace_{res.Uid}"
		};
		ws.BindBackingFile(virtualPath, assetUid);
		ws.m_pendingRootName = Path.GetFileNameWithoutExtension(virtualPath);
		return ws;
	}
}
