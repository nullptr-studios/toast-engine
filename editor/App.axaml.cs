using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using editor.Components.Modals;
using editor.StartWindow;
using editor.Workspace;

namespace editor;

public class App : Application {
	public static IModalService Modals { get; } = new ModalService();

	public static Window? MainWindow =>
		(Current?.ApplicationLifetime as IClassicDesktopStyleApplicationLifetime)?.MainWindow;

	public override void Initialize() {
		AvaloniaXamlLoader.Load(this);
	}

	public override void OnFrameworkInitializationCompleted() {
		PlayModeShortcuts.Install();

		if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop) {
			desktop.ShutdownMode = ShutdownMode.OnMainWindowClose;

			desktop.MainWindow = new StartWindow.StartWindow {
				DataContext = new StartWindowViewModel()
			};
			desktop.MainWindow.Show();
		}

		base.OnFrameworkInitializationCompleted();
	}
}
