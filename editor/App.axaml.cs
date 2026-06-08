using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using editor.StartWindow;

namespace editor;

public class App : Application {
	public override void Initialize() {
		AvaloniaXamlLoader.Load(this);
	}

	public override void OnFrameworkInitializationCompleted() {
		if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop) {
			desktop.ShutdownMode = ShutdownMode.OnLastWindowClose;

			var splashWindow = new StartWindow.StartWindow() {
				DataContext = new StartWindowViewModel()
			};
			splashWindow.Show();
		}

		base.OnFrameworkInitializationCompleted();
	}
}
