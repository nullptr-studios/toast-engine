using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using editor.ViewModels;
using editor.Views;

namespace editor;

public partial class App : Application {
	public override void Initialize() {
		AvaloniaXamlLoader.Load(this);
	}

	public override void OnFrameworkInitializationCompleted() {
		if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop) {
			desktop.ShutdownMode = ShutdownMode.OnLastWindowClose;

			var splash_window = new StartWindow {
				DataContext = new StartWindowViewModel()
			};
			splash_window.Show();
		}

		base.OnFrameworkInitializationCompleted();
	}
}