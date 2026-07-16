//
// Program.cs by Xein
// 12 May 2026
//

using System;
using Avalonia;

namespace editor;

internal sealed class Program {
	[STAThread]
	public static void Main(string[] args) {
		BuildAvaloniaApp()
			.StartWithClassicDesktopLifetime(args);
	}

	public static AppBuilder BuildAvaloniaApp() {
		return AppBuilder.Configure<App>()
			.UsePlatformDetect()
#if DEBUG
			.WithDeveloperTools()
#endif
			.WithInterFont()
			// HACK: Find a proper solution for this
			// renderdoc crashes when trying to attach to the internal avalonia
			// hardware accelerated renderer
			.With(new Win32PlatformOptions {
				RenderingMode = [ Win32RenderingMode.Software ]
			})
			.With(new X11PlatformOptions {
				RenderingMode = [ X11RenderingMode.Software ]
			})
			.LogToTrace();
	}
}
