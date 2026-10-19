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

		if (OperatingSystem.IsLinux()) {
			// GLX intermittently imports shared Vulkan frames as transparent black on
			// NVIDIA. EGL passes the same interop path; if it is unavailable, use the
			// software compositor so the viewport automatically uses readback.
			builder = builder.With(new X11PlatformOptions {
				RenderingMode = [ X11RenderingMode.Egl, X11RenderingMode.Software ]
			});
		}

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
