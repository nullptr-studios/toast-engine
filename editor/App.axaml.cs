using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Dock.Settings;
using editor.AssetBrowser;
using StartWindowClass = editor.StartWindow.StartWindow;
using StartWindowViewModelClass = editor.StartWindow.StartWindowViewModel;

namespace editor;

public partial class App : Application {
	public override void Initialize() {
		DockSettings.GlobalDockingPreset = DockGlobalDockingPreset.GlobalFirst;
		DockSettings.GlobalDockingProportion = 0.2;

		AvaloniaXamlLoader.Load(this);
	}

	public override void OnFrameworkInitializationCompleted() {
		if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop) {
			desktop.ShutdownMode = ShutdownMode.OnLastWindowClose;

			//var splash_window = new StartWindowClass {
			//	DataContext = new StartWindowViewModelClass()
			//};
			//splash_window.Show();

			var ab = new Import.ImportWindow();
			ab.Show();
		}

		base.OnFrameworkInitializationCompleted();
	}
}