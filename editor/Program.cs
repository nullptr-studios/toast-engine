//
// Program.cs by Xein
// 12 May 2026
//

using System;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using editor.Engine;
using editor.Git;
using editor.Install;

namespace editor;

internal sealed class Program {
	[STAThread]
	public static int Main(string[] args) {
		// Launched by git or ssh as the askpass program
		if (GitAskpass.IsHelperInvocation) return GitAskpass.RunHelper(args);

		// Used by the Linux install script
		if (args.Contains("--register")) {
			ShellIntegration.Apply();
			return 0;
		}

		Task.Run(ShellIntegration.Apply);

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

		// The DXGI swap chain ticks on the vblank of the fastest monitor where WinUI commits can lag the refresh
		var win32 = new Win32PlatformOptions {
			CompositionMode = [
				Win32CompositionMode.LowLatencyDxgiSwapChain,
				Win32CompositionMode.WinUIComposition,
				Win32CompositionMode.DirectComposition,
				Win32CompositionMode.RedirectionSurface
			]
		};

		// Force software when renderdoc
		if (GraphicsDebuggerDetector.IsAttached) {
			win32.RenderingMode = [ Win32RenderingMode.Software ];
			builder = builder.With(new X11PlatformOptions {
				RenderingMode = [ X11RenderingMode.Software ]
			});
		}

		builder = builder.With(win32);

		return builder.LogToTrace();
	}
}
