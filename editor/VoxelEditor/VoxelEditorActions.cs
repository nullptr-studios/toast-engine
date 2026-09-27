using System;
using System.IO;
using System.Threading.Tasks;
using editor.Assets;
using editor.Engine;
using editor.Workspace;
using Proto.Events;
using HierarchyElement = editor.Workspace.HierarchyElement;

namespace editor.VoxelEditor;

public static class VoxelEditorActions {
	private const string ProceduralVoxelType = "toast::ProceduralVoxel";

	public static void Register() {
		EditorActions.Register("voxel_editor.open", OpenAsync);
	}

	private static async Task OpenAsync(HierarchyElement node) {
		var isProcedural = node.Type == ProceduralVoxelType;

		// Already a ProceduralVoxel prefab, just open it
		if (isProcedural && node.IsPrefab && !string.IsNullOrEmpty(node.PrefabUid)) {
			VoxelEditorWindow.Open(node.PrefabUid, node.Owner.ActiveWorkspaceHandle, node.Uid);
			return;
		}

		// The root of an open prefab tab is that prefab
		if (isProcedural && node.IsRoot && node.Owner.ActiveWorkspace?.BackingAssetUid is { } backing) {
			VoxelEditorWindow.Open(backing);
			return;
		}

		if (node.IsRoot) {
			await App.Modals.ShowWarning("Edit Voxel",
				"The root of a workspace cannot become a prefab. Put it under another node first");
			return;
		}

		if (node.IsInsidePrefab) {
			await App.Modals.ShowWarning("Edit Voxel", "This shape belongs to another prefab, edit that prefab instead");
			return;
		}

		// Everything the VoxelEditor opens is a prefab so it can be reused across levels
		var virtualPath = await App.Modals.ShowSaveFile(node.Name);
		if (virtualPath is null) return;

		var realPath = ProjectContext.Resolve(virtualPath);
		Directory.CreateDirectory(Path.GetDirectoryName(realPath)!);
		File.WriteAllBytes(realPath, Array.Empty<byte>());
		MetaFile.Write(realPath, new MetaHeader { Uid = UidGenerator.Generate(), Type = "node" });
		AssetDatabase.RebuildAssetDatabase();
		Events.Send(new ReloadAssetsManifest());

		var promoted = await ConvertAsync(node.Uid, virtualPath);
		if (promoted is null || !promoted.Success) {
			await App.Modals.ShowError("Edit Voxel", promoted?.Error is { Length: > 0 } error
				? error
				: "The shape could not be turned into a prefab");
			return;
		}

		// The instance got a new uid when it was spawned, Show Others finds it through the prefab
		VoxelEditorWindow.Open(promoted.PrefabUid, promoted.WorkspaceHandle);
	}

	// wais for the engine to say the prefab is on disk
	private static async Task<ProceduralVoxelPromoted?> ConvertAsync(string nodeUid, string virtualPath) {
		var done = new TaskCompletionSource<ProceduralVoxelPromoted>(TaskCreationOptions.RunContinuationsAsynchronously);
		using var listener = new Listener();
		listener.SubscribeOnUiThread<ProceduralVoxelPromoted>(e => done.TrySetResult(e));
		Events.Send(new WorkspaceConvertToProceduralVoxel { Target = nodeUid, Path = virtualPath });

		var finished = await Task.WhenAny(done.Task, Task.Delay(TimeSpan.FromSeconds(10)));
		return finished == done.Task ? done.Task.Result : null;
	}
}
