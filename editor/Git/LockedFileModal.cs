using System.Threading.Tasks;
using editor.Components.Modals;
using Lucide.Avalonia;

namespace editor.Git;

public enum LockedSaveChoice {
	Cancel,
	Proceed,
	Discard,
	SaveAs
}

public static class LockedFileModal {
	public static Task<LockedSaveChoice> ShowAsync(string fileName, string owner) {
		return Show(new ModalConfig(
			"File locked",
			$"{fileName} is locked by {owner}.",
			ModalButtons.OkNoCancel,
			LucideIconKind.Lock,
			OkLabel: "Save As",
			NoLabel: "Discard",
			OkIcon: LucideIconKind.Save,
			NoIcon: LucideIconKind.Trash2), LockedSaveChoice.Discard, LockedSaveChoice.Cancel);
	}

	public static Task<LockedSaveChoice> ShowOpenAsync(string fileName, string owner) {
		return Show(new ModalConfig(
			"File locked",
			$"{fileName} is locked by {owner}.",
			ModalButtons.OkNoCancel,
			LucideIconKind.Lock,
			OkLabel: "Save As",
			NoLabel: "Open anyway",
			OkIcon: LucideIconKind.Save,
			NoIcon: LucideIconKind.FolderOpen), LockedSaveChoice.Proceed, LockedSaveChoice.Proceed);
	}

	private static async Task<LockedSaveChoice> Show(ModalConfig config, LockedSaveChoice no, LockedSaveChoice noWindow) {
		var parent = ModalService.FindActiveWindow();
		if (parent is null) return noWindow;

		return await new MessageModal(config).ShowDialog<bool?>(parent) switch {
			true => LockedSaveChoice.SaveAs,
			false => no,
			_ => LockedSaveChoice.Cancel
		};
	}
}
