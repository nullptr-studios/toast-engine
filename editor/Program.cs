//
// Program.cs by Xein
// 12 May 2026
//

using System;
using Avalonia;
using editor.Engine;

namespace editor;

internal sealed class Program {
	[STAThread]
	public static void Main(string[] args) {
		BuildAvaloniaApp()
			.StartWithClassicDesktopLifetime(args);
	}

	public static AppBuilder BuildAvaloniaApp() {
		var builder = AppBuilder.Configure<App>()
			.UsePlatformDetect()
#if DEBUG
			.WithDeveloperTools()
#endif
			.WithInterFont();

		// Force software when renderdoc
		if (RenderDocDetector.IsAttached) {
			builder = builder
				.With(new Win32PlatformOptions {
					RenderingMode = [ Win32RenderingMode.Software ]
				})
				.With(new X11PlatformOptions {
					RenderingMode = [ X11RenderingMode.Software ]
				});
		}

		return builder.LogToTrace();
	}
}
