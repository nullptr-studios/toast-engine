using System.Diagnostics;
using System.Threading;

namespace player;

internal class Program {
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
		var databasePath = Path.Combine(basePath, "game.db");
		if (!engine.OpenAssetDatabase(databasePath)) {
			Console.Error.WriteLine($"Could not load the game's asset database: {databasePath} (or a pak it lists) is missing");
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
