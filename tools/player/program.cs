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
		foreach (var pakFile in Directory.EnumerateFiles(basePath, "*.pak")) {
			var scheme = Path.GetFileNameWithoutExtension(pakFile);
			engine.MountPack(scheme, pakFile);
		}

		engine.Init();
		engine.CreateSdlWindow("Toast Engine");
		engine.StartGame();

		while (!engine.ShouldClose()) engine.Tick();

		engine.Dispose();
	}
}
