using System.Text;

namespace ReflectionGenerator.Output;

internal static class OutputFile
{
	// identical content keeps its timestamp so nothing downstream rebuilds
	public static void WriteIfChanged(string path, string content)
	{
		var bytes = new UTF8Encoding(false).GetBytes(content);
		try {
			if (File.Exists(path) && File.ReadAllBytes(path).AsSpan().SequenceEqual(bytes)) return;

			var directory = Path.GetDirectoryName(Path.GetFullPath(path));
			if (directory is not null) Directory.CreateDirectory(directory);
			File.WriteAllBytes(path, bytes);
		} catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
			throw new GeneratorException($"cannot write '{path}': {e.Message}");
		}
	}
}
