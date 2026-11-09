using ReflectionGenerator.Analysis;
using ReflectionGenerator.Model;
using ReflectionGenerator.Output;
using ReflectionGenerator.Parsing;
using ReflectionGenerator.Rules;

namespace ReflectionGenerator;

internal sealed class Pipeline(Options options)
{
	public void Run()
	{
		CheckDirectories();

		var headers = HeaderFinder.Find(options.Inputs);
		if (headers.Count == 0) {
			Warn("no [[ToastNode]] headers found in the given --input directories");
		}

		var (nodes, events) = ParseHeaders(headers);
		WriteOrWarn(options.Database, Database.Serialize(nodes, events));

		var ordered = InheritanceOrder.Sort(nodes);
		var templates = new Templates();

		new CppOutput(templates, options.Output, options.RegisterFn, options.SplitTypeInfo).Write(ordered, events);

		if (options.LuaStubs is { } luaStubs) {
			WriteOrWarn(luaStubs, LuaOutput.NodeStubs(templates, ordered));
		}
		if (options.EventLuaStubs is { } eventLuaStubs) {
			WriteOrWarn(eventLuaStubs, LuaOutput.EventStubs(templates, events));
		}

		Console.WriteLine($"refgen: generated {ordered.Count} type(s) -> '{options.Output}' (fn: {options.RegisterFn})");
	}

	private void CheckDirectories()
	{
		foreach (var input in options.Inputs.Where(input => !Directory.Exists(input))) {
			throw new GeneratorException($"--input '{input}' is not a directory");
		}
		if (!Directory.Exists(options.Output)) {
			throw new GeneratorException($"--output '{options.Output}' is not a directory");
		}
	}

	private (List<NodeType> Nodes, List<EventType> Events) ParseHeaders(IEnumerable<string> headers)
	{
		var nodes = new List<NodeType>();
		var events = new List<EventType>();

		foreach (var header in headers) {
			string source;
			try {
				source = File.ReadAllText(header);
			} catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
				Console.Error.WriteLine($"error reading '{header}': {e.Message}");
				continue;
			}

			var text = ExportMacros.Strip(source);
			var includePath = IncludePath(header);

			foreach (var node in NodeParser.Parse(text, includePath)) {
				Validation.Check(node);
				nodes.Add(node with { Attributes = options.Attributes.Aggregate(node.Attributes, (list, name) => list.WithMarker(name)) });
			}
			events.AddRange(EventParser.Parse(text, includePath));
		}

		return (nodes, DistinctEvents(events));
	}

	// the same event can be seen from several headers
	private static List<EventType> DistinctEvents(IEnumerable<EventType> events)
	{
		var distinct = events
			.OrderBy(e => e.QualifiedName, StringComparer.Ordinal)
			.GroupBy(e => e.QualifiedName)
			.Select(group => group.First())
			.ToList();

		var skipped = distinct.Count(e => !e.Supported);
		if (skipped > 0) {
			Warn($"skipped {skipped} Lua events");
		}
		return distinct;
	}

	// the include has to be relative to the include root to work from anywhere
	private string IncludePath(string file)
	{
		if (options.IncludeRoot is { } root) {
			var relative = Path.GetRelativePath(root, file);
			var outside = relative == ".." || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal);
			if (!outside && !Path.IsPathRooted(relative)) return relative.Replace('\\', '/');
		}
		return Path.GetFileName(file);
	}

	private static void WriteOrWarn(string path, string content)
	{
		try {
			OutputFile.WriteIfChanged(path, content);
		} catch (GeneratorException e) {
			Warn(e.Message);
		}
	}

	private static void Warn(string message) => Console.Error.WriteLine($"warning: {message}");
}
