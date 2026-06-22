using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Lucide.Avalonia;

namespace editor.Components.Modals;

public class ModalService : IModalService {
	public Task ShowInfo(string title, string message) {
		return Show(new ModalConfig(title, message, Icon: LucideIconKind.Info,
			IconColor: new SolidColorBrush(Color.Parse("#4a9eff"))));
	}

	public Task ShowWarning(string title, string message) {
		return Show(new ModalConfig(title, message, Icon: LucideIconKind.TriangleAlert,
			IconColor: new SolidColorBrush(Color.Parse("#f0a020"))));
	}

	public Task ShowError(string title, string message) {
		return Show(new ModalConfig(title, message, Icon: LucideIconKind.CircleAlert,
			IconColor: new SolidColorBrush(Color.Parse("#d04040"))));
	}

	public async Task<bool> ShowConfirm(string title, string message) {
		var result = await Show<bool?>(new ModalConfig(title, message,
			ModalButtons.OkCancel,
			LucideIconKind.CircleQuestionMark,
			new SolidColorBrush(Color.Parse("#4a9eff"))));
		return result == true;
	}

	public async Task<SaveChangesResult> ShowSaveChanges(string filename) {
		var result = await Show<bool?>(new ModalConfig(
			"Unsaved changes",
			$"Save changes to \"{filename}\" before closing?",
			ModalButtons.OkNoCancel,
			LucideIconKind.Save,
			new SolidColorBrush(Color.Parse("#4a9eff")),
			"Save",
			"Don't Save",
			"Cancel"
		));
		return result switch {
			true => SaveChangesResult.Save,
			false => SaveChangesResult.DontSave,
			null => SaveChangesResult.Cancel
		};
	}

	public async Task<string?> ShowSaveFile(string defaultPath) {
		var window = new SaveFileModal(defaultPath);
		var owner = FindActiveWindow();
		if (owner is null) return null;
		return await window.ShowDialog<string?>(owner);
	}

	private Task Show(ModalConfig cfg) {
		return Show<bool?>(cfg);
	}

	private Task<T?> Show<T>(ModalConfig cfg) {
		var window = new MessageModal(cfg);
		var owner = FindActiveWindow();
		if (owner is not null)
			return window.ShowDialog<T>(owner).ContinueWith(t => (T?)t.Result);
		window.Show();
		return Task.FromResult<T?>(default);
	}

	// prefer the focused window, fall back to anything open
	private static Window? FindActiveWindow() {
		if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
			return desktop.Windows.FirstOrDefault(w => w.IsActive)
			       ?? desktop.Windows.FirstOrDefault();
		return null;
	}
}
