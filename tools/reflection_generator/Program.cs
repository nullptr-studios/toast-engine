using System.Text;

namespace ReflectionGenerator;

internal static class Program
{
	private static int Main(string[] args)
	{
		Console.OutputEncoding = new UTF8Encoding(false);

		if (args.Contains("--help") || args.Contains("-h")) {
			Console.WriteLine(Options.Usage);
			return 0;
		}

		try {
			new Pipeline(Options.Parse(args)).Run();
			return 0;
		} catch (GeneratorException e) {
			Console.Error.WriteLine($"error: {e.Message}");
			return 1;
		}
	}
}
