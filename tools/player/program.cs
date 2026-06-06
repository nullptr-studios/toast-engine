namespace player;

internal class Program {
	public static void Main() {
		var engine = new ToastEngine();
		var game = new ApplicationLayer();

		while (!engine.ShouldClose()) engine.Tick();

		Console.WriteLine("Exiting application...");
	}
}
