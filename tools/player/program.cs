namespace player;

internal class Program {
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

		while (!engine.ShouldClose()) engine.Tick();

		game.Dispose();
		engine.Dispose();
	}
}
