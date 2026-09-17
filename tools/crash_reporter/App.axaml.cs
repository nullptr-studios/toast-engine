using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;

namespace crash_reporter;

public class App : Application {
	internal static CrashTarget? Target { get; set; }

	public override void Initialize() {
		AvaloniaXamlLoader.Load(this);
	}

	public override void OnFrameworkInitializationCompleted() {
		if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop && Target is not null) {
			desktop.MainWindow = new CrashWindow(Target);
		}

		base.OnFrameworkInitializationCompleted();
	}
}
