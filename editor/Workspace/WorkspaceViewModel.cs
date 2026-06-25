using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Components.Modals;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public class WorkspaceViewModel : Document {
	private string? m_rootUid;

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

	// called by HierarchyViewModel whenever the hierarchy tree updates
	public void SetRootNode(string uid) {
		m_rootUid = uid;
	}

	public void BindBackingFile(string virtualUri, string assetUid) {
		BackingUri = virtualUri;
		BackingAssetUid = assetUid;
	}

	public override bool OnClose() {
		Events.Send(new SetFocusedNode { Node = "" });
		Events.Send(new WorkspaceDestroy { Handle = Handle });
		return base.OnClose();
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
				return true;
		}
	}

	public async Task Save() {
		if (m_rootUid is null) return;

		if (BackingUri is null) {
			await SaveAs(); // no path yet -> prompt the user
			return;
		}

		Events.Send(new WorkspaceSave { Target = m_rootUid, Path = BackingUri });
		IsModified = false;
	}

	public async Task SaveAs() {
		if (m_rootUid is null) return;

		var virtualPath = await App.Modals.ShowSaveFile(Title ?? "Unnamed Node");
		if (virtualPath is null) return;

		var realPath = ProjectContext.Resolve(virtualPath);

		Directory.CreateDirectory(Path.GetDirectoryName(realPath)!);
		// engine needs the file to exist on disk before it will write to it
		File.WriteAllBytes(realPath, Array.Empty<byte>());

		var uid = UidGenerator.Generate();
		MetaFile.Write(realPath, new MetaHeader { Uid = uid, Type = "node" });
		AssetDatabase.RebuildAssetDatabase();

		Events.Send(new ReloadAssetsManifest());
		Events.Send(new WorkspaceSave { Target = m_rootUid, Path = virtualPath });

		BackingUri = virtualPath;
		BackingAssetUid = uid;
		IsModified = false;
		ProjectContext.RaiseAssetsChanged();
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
	public static WorkspaceViewModel? OpenFile(ToastEngine engine, string assetUid, string virtualPath) {
		var res = engine.OpenWorkspace(assetUid);
		if (res.Uid == 0) return null;
		var ws = new WorkspaceViewModel(engine) {
			Handle = res.Uid,
			Title = Marshal.PtrToStringUTF8(res.Name) ?? "Unnamed Node",
			Id = $"Workspace_{res.Uid}"
		};
		ws.BindBackingFile(virtualPath, assetUid);
		return ws;
	}
}
