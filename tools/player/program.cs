using System.Diagnostics;
using System.Threading;

namespace player;

internal class Program {
	// Caps the tick loop rate to 500
	private const double TargetTickHz = 500.0;

	public static void Main() {
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
		foreach (var pakFile in Directory.EnumerateFiles(basePath, "*.pak")) {
			var scheme = Path.GetFileNameWithoutExtension(pakFile);
			engine.MountPack(scheme, pakFile);
		}

		engine.Init();
		engine.CreateSdlWindow("Toast Engine");
		engine.StartGame();

		var targetInterval = TimeSpan.FromSeconds(1.0 / TargetTickHz);
		var stopwatch = Stopwatch.StartNew();
		var nextTick = stopwatch.Elapsed + targetInterval;

		while (!engine.ShouldClose()) {
			engine.Tick();

			var remaining = nextTick - stopwatch.Elapsed;
			if (remaining > TimeSpan.Zero) {
				if (remaining > TimeSpan.FromMilliseconds(2))
					Thread.Sleep(remaining - TimeSpan.FromMilliseconds(1));
				while (stopwatch.Elapsed < nextTick) Thread.SpinWait(50);
				nextTick += targetInterval;
			} else {
				// fell behind, resync instead of bursting to catch up
				nextTick = stopwatch.Elapsed + targetInterval;
			}
		}

		game.Dispose();
		engine.Dispose();
	}
}
