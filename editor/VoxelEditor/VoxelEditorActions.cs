using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using editor.Assets;
using editor.Assets.Types;
using editor.Engine;
using editor.Workspace;
using Proto.Events;
using HierarchyElement = editor.Workspace.HierarchyElement;

namespace editor.VoxelEditor;

public static class VoxelEditorActions {
	private const string ProceduralVoxelType = "toast::ProceduralVoxel";
	private const string VoxelBrushType = "toast::VoxelBrush";
	private static readonly Listener s_listener = new();
	private static bool s_registered;

	public static void Register() {
		if (s_registered) return;
		s_registered = true;
		EditorActions.Register("voxel_editor.open", OpenAsync);
		EditorActions.Register("voxel_editor.bake", BakeAsync);
		s_listener.SubscribeOnUiThread<VoxelBakeCompleted>(OnBakeCompleted);
	}

	private static async void OnBakeCompleted(VoxelBakeCompleted completed) {
		try {
			if (completed.Unused.Count > 0) {
				foreach (var unused in completed.Unused) {
					var unusedPath = ProjectContext.Resolve(unused);
					AssetDatabase.TryDelete(unusedPath);
					AssetDatabase.TryDelete(unusedPath + ".meta");
				}
				AssetDatabase.RebuildAssetDatabase();
				Events.Send(new ReloadAssetsManifest());
			}
			if (string.IsNullOrEmpty(completed.Path)) return;

			var realPath = ProjectContext.Resolve(completed.Path);
			var header = MetaFile.ReadHeader(realPath);
			if (header is null) return;
			await Task.Run(() => new VoxelModelAsset().GenerateThumbnail(realPath, header.Uid));
		} catch (Exception e) {
			Log.Warn($"Could not generate thumbnail for {completed.Path}: {e.Message}");
		}
	}

	private static async Task BakeAsync(HierarchyElement node) {
		if (node.Type != ProceduralVoxelType) {
			await App.Modals.ShowWarning("Bake Voxel", "Only a ProceduralVoxel can be baked");
			return;
		}
		if (node.IsInsidePrefab) {
			await App.Modals.ShowWarning("Bake Voxel", "This shape belongs to another prefab, edit that prefab instead");
			return;
		}

		var chosen = await App.Modals.ShowSaveFile($"{node.Name}_baked", ".tnode");
		if (chosen is null) return;

		var folder = chosen.EndsWith(PrefabExtension, StringComparison.OrdinalIgnoreCase)
			? chosen[..^PrefabExtension.Length]
			: chosen;
		var name = folder[(folder.LastIndexOf('/') + 1)..];
		var prefabPath = $"{folder}/{name}{PrefabExtension}";

		var bake = new VoxelBake { Target = node.Uid, Path = prefabPath, Replace = true };
		var taken = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { name, "Model" };

		bake.BasePath = $"{folder}/Model.tvox";
		CreateVoxelFile(bake.BasePath);

		// Every volume gets a model
		void Walk(HierarchyElement parent) {
			foreach (var child in parent.Children) {
				if (child.Type is "toast::FillVolume" or "toast::VoxelMesh") {
					var path = $"{folder}/{UniqueFileName(child.Name, taken)}.tvox";
					CreateVoxelFile(path);
					bake.Pieces.Add(new VoxelBakePiece { Uid = child.Uid, Path = path });
				}
				if (child.Type is "toast::VoxelGroup" or "toast::FillVolume" or "toast::VoxelMesh" or "toast::CarveVolume"
				    or "toast::PaintVolume") {
					Walk(child);
				}
			}
		}

		Walk(node);

		var prefabReal = ProjectContext.Resolve(prefabPath);
		Directory.CreateDirectory(Path.GetDirectoryName(prefabReal)!);
		File.WriteAllBytes(prefabReal, Array.Empty<byte>());
		MetaFile.Write(prefabReal, new MetaHeader { Uid = UidGenerator.Generate(), Type = "node" });

		AssetDatabase.RebuildAssetDatabase();
		Events.Send(new ReloadAssetsManifest());
		Events.Send(bake);
	}

	private const string PrefabExtension = ".tnode";

	private static string UniqueFileName(string name, HashSet<string> taken) {
		var invalid = Path.GetInvalidFileNameChars();
		var clean = new string(name.Trim().Select(c => invalid.Contains(c) || c == ' ' ? '_' : c).ToArray());
		if (clean.Length == 0) clean = "Piece";
		var candidate = clean;
		for (var i = 2; !taken.Add(candidate); i++) candidate = $"{clean}_{i}";
		return candidate;
	}

	private static void CreateVoxelFile(string virtualPath) {
		var realPath = ProjectContext.Resolve(virtualPath);
		Directory.CreateDirectory(Path.GetDirectoryName(realPath)!);
		File.WriteAllBytes(realPath, Array.Empty<byte>());
		MetaFile.Write(realPath, new MetaHeader { Uid = UidGenerator.Generate(), Type = "voxel_model" });
	}

	internal static void CreateVoxelAsset(string virtualPath) {
		CreateVoxelFile(virtualPath);
		AssetDatabase.RebuildAssetDatabase();
		Events.Send(new ReloadAssetsManifest());
	}

	private static async Task OpenAsync(HierarchyElement node) {
		var isProcedural = node.Type is ProceduralVoxelType or VoxelBrushType;

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
