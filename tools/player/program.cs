namespace player;

class Program {
	public static void Main() {
		Console.WriteLine("Printing from C#");
		var engine = new ToastEngine();
		var game = new ApplicationLayer();

		while (!engine.ShouldClose()) {
			engine.Tick();
		}

		Console.WriteLine("Exiting application...");
	}
}
