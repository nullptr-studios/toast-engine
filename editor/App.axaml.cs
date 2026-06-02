using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using StartWindowClass = editor.StartWindow.StartWindow;
using StartWindowViewModelClass = editor.StartWindow.StartWindowViewModel;

namespace editor;

public partial class App : Application {
	public override void Initialize() {
		AvaloniaXamlLoader.Load(this);
	}

	public override void OnFrameworkInitializationCompleted() {
		if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop) {
			desktop.ShutdownMode = ShutdownMode.OnLastWindowClose;

			var splash_window = new StartWindowClass {
				DataContext = new StartWindowViewModelClass()
			};
			splash_window.Show();
		}

		base.OnFrameworkInitializationCompleted();
	}
}