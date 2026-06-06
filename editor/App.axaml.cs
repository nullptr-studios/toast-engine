using System;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Dock.Settings;
using editor.AssetBrowser;
using editor.Services;

namespace editor;

public class App : Application {
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

			// Temporary hardcoded init
			if (!ProjectContext.IsInitialized)
				ProjectContext.Initialize(
					@"C:\Users\Xein\Desktop\unnamed_project",
					Path.Combine(AppContext.BaseDirectory, "..", "toast_engine", "bin", "assets"));

			var ab = new AssetBrowser.AssetBrowser {
				DataContext = new AssetBrowserViewModel()
			};
			ab.Show();
		}

		base.OnFrameworkInitializationCompleted();
	}
}
