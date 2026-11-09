namespace ReflectionGenerator.Parsing;

internal static class HeaderFinder
{
	public static IReadOnlyList<string> Find(IEnumerable<string> inputDirs)
	{
		var found = new List<string>();
		foreach (var dir in inputDirs) {
			// sorted so the generated files do not shuffle between machines
			var headers = Directory.EnumerateFiles(dir, "*.hpp", SearchOption.AllDirectories)
				.Where(path => path.EndsWith(".hpp", StringComparison.Ordinal))
				.Order(StringComparer.Ordinal);

			foreach (var path in headers) {
				if (ReadOrNull(path) is { } content && (content.Contains("ToastNode") || content.Contains("Event<"))) {
					found.Add(path);
				}
			}
		}
		return found;
	}

	private static string? ReadOrNull(string path)
	{
		try {
			return File.ReadAllText(path);
		} catch (IOException) {
			return null;
		} catch (UnauthorizedAccessException) {
			return null;
		}
	}
}
