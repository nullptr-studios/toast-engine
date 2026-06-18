//
// ViewportViewModel.cs by Xein
// 2 Jun 2026
//

using System;
using System.IO;
using System.Threading.Tasks;
using Dock.Model.Mvvm.Controls;
using editor.Services;

namespace editor.Workspace;

// TODO: rename to `Workspace` to mirror the C++ class once it also owns the handle/despawning
public class ViewportViewModel : Document {
	public ViewportViewModel(ToastEngine? engine = null) {
		Engine = engine;
		Title = "Unnamed Node";
		CanDrag = false;
	}

	public ToastEngine? Engine { get; }

	public ulong Handle { get; init; }

	public bool PendingClose { get; set; }

	public override bool OnClose() {
		Events.Send(new Proto.Events.WorkspaceDestroy { Handle = Handle });
		return base.OnClose();
	}

	public void BindBackingFile(string virtualUri, string assetUid) {
		BackingUri = virtualUri;
		BackingAssetUid = assetUid;
	}

	public string? BackingUri { get; private set; }

	public string? BackingAssetUid { get; private set; }

	public async Task Save(HierarchyElement root) {
		if (BackingUri is null) {
			await SaveAs(root);
			return;
		}

		Events.Send(new Proto.Events.WorkspaceSave { Target = root.Uid, Path = BackingUri });
		IsModified = false;
	}

	public async Task SaveAs(HierarchyElement root) {
		if (App.MainWindow is not { } window) return;

		var dialog = new SaveWorkspaceDialog(root.Name);
		var virtualPath = await dialog.ShowDialog<string?>(window);
		if (virtualPath is null) return;    // cancelled

		var realPath = ProjectContext.Resolve(virtualPath);

		Directory.CreateDirectory(Path.GetDirectoryName(realPath)!);
		File.WriteAllBytes(realPath, Array.Empty<byte>());

		var uid = UidGenerator.Generate();
		MetaFile.Write(realPath, new MetaHeader { Uid = uid, Type = "node" });
		AssetDatabase.RebuildAssetDatabase();

		Events.Send(new Proto.Events.ReloadAssetsManifest());
		Events.Send(new Proto.Events.WorkspaceSave { Target = root.Uid, Path = virtualPath });

		BackingUri = virtualPath;
		BackingAssetUid = uid;
		IsModified = false;
		ProjectContext.RaiseAssetsChanged();
	}
}
