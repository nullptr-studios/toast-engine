using System.Diagnostics;
using System.Threading;

namespace player;

internal class Program {
	// Caps the tick loop rate to 500
	private const double TargetTickHz = 500.0;

	public static void Main() {
		var engine = new ToastEngine();
		var game = new ApplicationLayer();

		// TODO: Here we would set this to use .pack folders
		var path = AppContext.BaseDirectory;
		var assets = Directory.CreateDirectory(Path.Combine(path, "assets"));
		var cached = Directory.CreateDirectory(Path.Combine(path, "cached"));
		var core = Directory.CreateDirectory(Path.Combine(path, "core"));
		var saveData = Directory.CreateDirectory(Path.Combine(path, "saveData"));
		engine.SetWorkingDirectory(
			assets: assets.FullName,
			cached: cached.FullName,
			core: core.FullName,
			saved: saveData.FullName,
			artworks: ""
		);

		engine.Init();
		engine.CreateSdlWindow("Toast Engine");

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
