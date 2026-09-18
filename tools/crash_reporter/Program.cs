using Avalonia;

namespace crash_reporter;

internal static class Program {
	[STAThread]
	public static int Main(string[] args) {
		var crash = CrashArgs.Parse(args);
		if (crash is null) {
			Console.Error.WriteLine(
				"usage: crash_reporter --pid <pid> --tid <tid> --pointers <address> --message <address> --kind <kind> [--dump-dir <dir>]\n" +
				"Launched by the engine's crash handler (engine/src/toast/crash_handler.cpp), not by hand"
			);
			return 2;
		}

		// Resumes the crashed process however this exits so it does not stay frozen
		using var target = CrashTarget.Open(crash);
		AppDomain.CurrentDomain.UnhandledException += (_, _) => target.Dispose();

		App.Target = target;
		return BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
	}

	public static AppBuilder BuildAvaloniaApp() {
		return AppBuilder.Configure<App>()
			.UsePlatformDetect()
			.WithInterFont()
			.LogToTrace();
	}
}
