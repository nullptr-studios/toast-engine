//
// Program.cs by Xein
// 12 May 2026
//

using System;
using Avalonia;
using editor.Engine;
using editor.Git;

namespace editor;

internal sealed class Program {
	[STAThread]
	public static int Main(string[] args) {
		// Launched by git or ssh as the askpass program
		if (GitAskpass.IsHelperInvocation) return GitAskpass.RunHelper(args);

		return BuildAvaloniaApp()
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
		if (GraphicsDebuggerDetector.IsAttached) {
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
