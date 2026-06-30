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
			.LogToTrace();
	}
}
