//
// MainWindowViewModel.cs by Xein
// 12 May 2026
//

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Controls;
using editor.AssetBrowser;
using editor.Services;

namespace editor.Workspace;

public partial class MainWindowViewModel : ViewModelBase {
	private readonly ToastEngine m_toast;
	private readonly DockFactory m_dockFactory;
	private readonly ToastZoneFactory m_toastZoneFactory;
	private readonly Dictionary<ulong, ViewportViewModel> m_viewports = [];
	public IRootDock MainLayout { get; set; }
	public IRootDock ToastZoneLayout { get; set; }

	public ulong CreateWorkspace(string type) {
		var res = toast_create_workspace(type);
		m_viewports[res.Uid] = m_dockFactory.AddViewport(res.Uid, Marshal.PtrToStringUTF8(res.Name) ?? "Unnamed Node");
		SyncActiveWorkspace();
		return res.Uid;
	}

	public ulong OpenWorkspace(string assetUid) {
		var res = toast_open_workspace(assetUid);
		if (res.Uid == 0) return 0;    // failed to load, or already open
		m_viewports[res.Uid] = m_dockFactory.AddViewport(res.Uid, Marshal.PtrToStringUTF8(res.Name) ?? "Unnamed Node");
		SyncActiveWorkspace();
		return res.Uid;
	}

	[ObservableProperty]
	private bool m_toastZoneActive;

	private bool m_toastZonePinned;

	public MainWindowViewModel(ToastEngine toast) {
		m_toast = toast;

		// Main docking area
		m_dockFactory = new DockFactory(toast);
		MainLayout = m_dockFactory.CreateLayout();
		m_dockFactory.InitLayout(MainLayout);

		// Toast docking area
		m_toastZoneFactory = new ToastZoneFactory();
		ToastZoneLayout = m_toastZoneFactory.CreateLayout();
		m_toastZoneFactory.InitLayout(ToastZoneLayout);

		// Closing a viewport prompts to save when modified then destroys the workspace
		m_dockFactory.ConfirmClose = ConfirmCloseAsync;
		m_dockFactory.DockableClosed += (_, e) => {
			if (e.Dockable is ViewportViewModel vp) m_viewports.Remove(vp.Handle);
			SyncActiveWorkspace();
		};

		// Tell the engine which workspace is focused
		m_dockFactory.ActiveDockableChanged += (_, _) => SyncActiveWorkspace();

		// Any edit to the active workspace marks its viewport as having unsaved changes
		WorkspaceState.Modified += () => {
			if (m_dockFactory.ActiveViewport is { } vp) vp.IsModified = true;
		};
	}

	private ulong m_activeWorkspaceHandle;

	private void SyncActiveWorkspace() {
		var handle = m_dockFactory.ActiveViewport?.Handle ?? 0;
		if (handle == m_activeWorkspaceHandle) return;
		m_activeWorkspaceHandle = handle;

		Events.Send(new Proto.Events.SetActiveWorkspace { Handle = handle });
		if (handle == 0) m_dockFactory.Hierarchy?.Clear();
	}

	// Save / Don't Save / Cancel prompt for a modified viewport
	private async Task<bool> ConfirmCloseAsync(ViewportViewModel vp) {
		if (!vp.IsModified) return true;
		if (App.MainWindow is not { } window) return true;

		var result = await new SaveChangesDialog(vp.Title ?? "Unnamed Node")
			.ShowDialog<SaveChangesResult>(window);

		switch (result) {
			case SaveChangesResult.Cancel:
				return false;
			case SaveChangesResult.Save:
				if (vp == m_dockFactory.ActiveViewport && m_dockFactory.Hierarchy is { Root.Count: > 0 } h)
					await vp.Save(h.Root[0]);
				return true;
			default:    // DontSave
				return true;
		}
	}

	[RelayCommand]
	private void CloseNodeFile() {
		if (m_dockFactory.ActiveViewport is { } vp) m_dockFactory.CloseDockable(vp);
	}

	public void ShowToastZone(bool active) {
		if (m_toastZonePinned == true) return;
		ToastZoneActive = active;
	}

	public void PinToastZone() {
		m_toastZonePinned = !m_toastZonePinned;
		ToastZoneActive = m_toastZonePinned;
	}

	[RelayCommand]
	private async Task NewNode(Window parent) {
		var popup = new NewNodeView();
		var result = await popup.ShowDialog<string?>(parent);

		if (result is null) {
			// We cancel the creation of a new node
			return;
		}

		var handle = CreateWorkspace(result);
		m_viewports[handle].IsModified = true;
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

		var header = MetaFile.ReadHeader(files[0].Path.LocalPath);
		if (header is null) return;

		var handle = OpenWorkspace(header.Uid);
		if (handle != 0 && m_viewports.TryGetValue(handle, out var vp)
		    && ProjectContext.ToVirtual(files[0].Path.LocalPath) is { } virtualPath)
			vp.BindBackingFile(virtualPath, header.Uid);
	}

	[StructLayout(LayoutKind.Sequential)]
	public struct WorkspaceResult {
		public ulong Uid;
		public nint Name;
	}

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial WorkspaceResult toast_create_workspace(string type);

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial WorkspaceResult toast_open_workspace(string uid);
}
