using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public class WorkspaceViewModel : Document, IAutosavable {
	private string? m_pendingRootName;

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
