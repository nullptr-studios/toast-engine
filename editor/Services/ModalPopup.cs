using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Lucide.Avalonia;

namespace editor.Services;

// Throw from anywhere, no window reference required
// Blocks the calling await until the user dismisses the dialog
public static class ModalPopup {
	public static Task ShowInfo(string title, string message, Window? owner = null) {
		return Show(title, message, owner, LucideIconKind.Info, new SolidColorBrush(Color.Parse("#4a9eff")), false);
	}

	public static Task ShowWarning(string title, string message, Window? owner = null) {
		return Show(title, message, owner, LucideIconKind.TriangleAlert, new SolidColorBrush(Color.Parse("#f0a020")),
			false);
	}

	public static Task ShowError(string title, string message, Window? owner = null) {
		return Show(title, message, owner, LucideIconKind.CircleAlert, new SolidColorBrush(Color.Parse("#d04040")),
			false);
	}

	public static async Task<bool> ShowConfirm(string title, string message, Window? owner = null) {
		var result = await Show(title, message, owner, LucideIconKind.CircleQuestionMark,
			new SolidColorBrush(Color.Parse("#4a9eff")), true);
		return result;
	}

	private static Task<bool> Show(
		string title, string message, Window? owner,
		LucideIconKind icon, IBrush iconColor, bool showCancel) {
		var vm = new ModalPopupViewModel {
			Title = title,
			Message = message,
			IconKind = icon,
			IconColor = iconColor,
			ShowCancel = showCancel
		};
		var window = new ModalPopupWindow(vm);
		var ownerWindow = owner ?? FindActiveWindow();

		if (ownerWindow is not null)
			return window.ShowDialog<bool>(ownerWindow).ContinueWith(t => t.Result);

		window.Show();
		return Task.FromResult(false);
	}

	private static Window? FindActiveWindow() {
		if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
			return desktop.Windows.FirstOrDefault(w => w.IsActive)
			       ?? desktop.Windows.FirstOrDefault();
		return null;
	}
}
