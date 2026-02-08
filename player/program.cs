namespace player;

class Program {
	public static void Main() {
		Console.WriteLine("Printing from C#");
		var engine = new ToastEngine();

		while (!engine.ShouldClose()) {
			engine.Tick();
		}

		Console.WriteLine("Exiting application...");
	}
}
