using System;
using System.IO;
using System.Threading.Tasks;
using editor.Assets;
using editor.Assets.Types;
using editor.Engine;
using editor.Workspace;

namespace editor.Git;

public static class GitLockGuard {
	private static GitService? Usable => GitService.Current is { IsAvailable: true, HasLfs: true } git ? git : null;

	public static async Task LockOnOpenAsync(string realPath) {
		if (Usable is not { } git) return;
		if (git.GetLock(realPath) is not null || !git.IsLockable(realPath)) return;

		var result = await git.LockAsync(realPath);
		if (!result.Ok) Log.Warn($"Git: could not lock {Path.GetFileName(realPath)} on open ({result.Message})");
	}

	public static async Task<LockedSaveChoice> CheckSaveAsync(string realPath) {
		if (Usable is not { } git) return LockedSaveChoice.Proceed;

		var held = git.GetLock(realPath);
		if (held is { OwnedByMe: true }) return LockedSaveChoice.Proceed;

		if (held is null) {
			if (!git.IsLockable(realPath)) return LockedSaveChoice.Proceed;

			var result = await git.LockAsync(realPath);
			if (result.Ok) return LockedSaveChoice.Proceed;

			// The cached locks may be old
			held = git.GetLock(realPath);
			if (held is null) {
				// Offline or server trouble must not stop people from saving their work
				Log.Warn($"Git: could not lock {Path.GetFileName(realPath)} on save ({result.Message})");
				return LockedSaveChoice.Proceed;
			}

			if (held.OwnedByMe) return LockedSaveChoice.Proceed;
		} else {
			// It may have been released since the last poll
			await git.RefreshLocksAsync();
			held = git.GetLock(realPath);
			if (held is null) return await CheckSaveAsync(realPath);
			if (held.OwnedByMe) return LockedSaveChoice.Proceed;
		}

		return await LockedFileModal.ShowAsync(Path.GetFileName(realPath), held.Owner);
	}

	public static BaseAsset? DefinitionOf(string virtualPath) {
		return AssetTypeRegistry.ByExtension(AssetTypeRegistry.GetExtension(virtualPath));
	}

	public static async Task<bool> SaveAsync(string uid, string virtualPath, Func<string, Task> write,
		Action<string, string> reopen) {
		var ext = AssetTypeRegistry.GetExtension(virtualPath);
		var realPath = ProjectContext.Resolve(virtualPath);

		switch (await CheckSaveAsync(realPath)) {
			case LockedSaveChoice.Cancel:
				return false;
			case LockedSaveChoice.Discard:
				AutosaveService.Delete(uid, ext);
				reopen(uid, virtualPath);
				return true;
			case LockedSaveChoice.SaveAs:
				return await SaveCopyAsync(uid, virtualPath, write, reopen);
		}

		await write(realPath);
		MetaFile.Touch(virtualPath);
		AutosaveService.Delete(uid, ext);
		return true;
	}

	private static async Task<bool> SaveCopyAsync(string uid, string virtualPath, Func<string, Task> write,
		Action<string, string> reopen) {
		var ext = AssetTypeRegistry.GetExtension(virtualPath);
		var target = await App.Modals.ShowSaveFile(Path.GetFileName(virtualPath), ext);
		if (target is null) return false;

		var realPath = ProjectContext.Resolve(target);
		if (File.Exists(realPath)) {
			await App.Modals.ShowError("Save As", $"{target} already exists, pick another name.");
			return false;
		}

		Directory.CreateDirectory(Path.GetDirectoryName(realPath)!);
		await write(realPath);

		var newUid = UidGenerator.Generate();
		MetaFile.Write(realPath, new MetaHeader { Uid = newUid, Type = DefinitionOf(virtualPath)?.Type ?? "" });
		AssetDatabase.RebuildAssetDatabase();
		ProjectContext.RaiseAssetsChanged();
		AutosaveService.Delete(uid, ext);

		// Saving takes the lock on the copy
		await LockOnOpenAsync(realPath);
		reopen(newUid, target);
		return true;
	}
}
