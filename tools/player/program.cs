using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

namespace player;

internal class Program {
	[DllImport("SDL3", CallingConvention = CallingConvention.Cdecl)]
	[return: MarshalAs(UnmanagedType.U1)]
	private static extern bool SDL_ShowSimpleMessageBox(uint flags, [MarshalAs(UnmanagedType.LPUTF8Str)] string title,
		[MarshalAs(UnmanagedType.LPUTF8Str)] string message, IntPtr window);

	public static void Main() {
		// Makes slang's mimalloc return freed compile memory at once
		if (Environment.GetEnvironmentVariable("MIMALLOC_PURGE_DELAY") is null) Environment.SetEnvironmentVariable("MIMALLOC_PURGE_DELAY", "0");
		var engine = new ToastEngine();
		var game = new ApplicationLayer();

		var basePath = AppContext.BaseDirectory;
		var cacheDir  = Directory.CreateDirectory(Path.Combine(basePath, "cache")).FullName;
		var savedDir  = Directory.CreateDirectory(Path.Combine(basePath, "saveData")).FullName;

		engine.SetWorkingDirectory(
			project:  basePath,
			artworks: "",
			cache:    cacheDir,
			saved:    savedDir,
			core:     basePath
		);

		engine.SetLoadMode(gameMode: true);
		var databasePath = Path.Combine(basePath, "game.db");
		if (!engine.OpenAssetDatabase(databasePath)) {
			var message = $"Could not load the game's asset database: {databasePath} (or a pak it lists) is missing";
			Console.Error.WriteLine(message);
			// WinExe has no console to show the line above
			try { SDL_ShowSimpleMessageBox(0x10 /* SDL_MESSAGEBOX_ERROR */, "Toast Engine", message, IntPtr.Zero); } catch (DllNotFoundException) { }
			engine.Dispose();
			return;
		}

		engine.Init();
		engine.CreateSdlWindow("Toast Engine");
		engine.StartGame();

		var stopwatch = Stopwatch.StartNew();
		var nextTick = stopwatch.Elapsed;

		while (!engine.ShouldClose()) {
			engine.Tick();

			var maxRate = engine.MaxTickRate;
			if (maxRate == 0) {
				nextTick = stopwatch.Elapsed;
				continue;
			}

			var targetInterval = TimeSpan.FromSeconds(1.0 / maxRate);
			nextTick += targetInterval;
			var remaining = nextTick - stopwatch.Elapsed;
			if (remaining > TimeSpan.Zero) {
				if (remaining > TimeSpan.FromMilliseconds(2))
					Thread.Sleep(remaining - TimeSpan.FromMilliseconds(1));
				while (stopwatch.Elapsed < nextTick) Thread.SpinWait(50);
			} else {
				// fell behind, resync instead of bursting to catch up
				nextTick = stopwatch.Elapsed;
			}
		}

		game.Dispose();
		engine.Dispose();
	}
}
