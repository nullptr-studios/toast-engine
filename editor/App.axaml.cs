using Avalonia;
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
			//desktop.MainWindow = new StartWindow {
			//	DataContext = new StartWindowViewModel(),
			//};

			desktop.MainWindow = new SplashWindow();
		}

		base.OnFrameworkInitializationCompleted();
	}
}