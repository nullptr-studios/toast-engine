using ReflectionGenerator.Analysis;
using ReflectionGenerator.Model;
using ReflectionGenerator.Output;
using ReflectionGenerator.Parsing;
using ReflectionGenerator.Rules;
using Spectre.Console;

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

		IReadOnlyList<NodeType> ordered = [];

		try {
			AnsiConsole.Progress()
				.AutoClear(true)
				.Columns(new BarColumn())
				.Start(ctx => {
					var task = ctx.AddTask("starting", maxValue: headers.Count + 3);
					void Step(string what) {
						task.Description = what;
						task.Increment(1);
					}

					var (nodes, events) = ParseHeaders(headers, Step);

					Step("database");
					WriteOrWarn(options.Database, Database.Serialize(nodes, events));

					ordered = InheritanceOrder.Sort(nodes);
					var templates = new Templates();

					Step("cpp");
					new CppOutput(templates, options.Output, options.RegisterFn, options.SplitTypeInfo).Write(ordered, events);

					Step("lua stubs");
					if (options.LuaStubs is { } luaStubs) {
						WriteOrWarn(luaStubs, LuaOutput.NodeStubs(templates, ordered));
					}
					if (options.EventLuaStubs is { } eventLuaStubs) {
						WriteOrWarn(eventLuaStubs, LuaOutput.EventStubs(templates, events));
					}
				});
		} finally {
			foreach (var message in m_messages) Console.Error.WriteLine(message);
		}

		if (!Console.IsOutputRedirected) Console.WriteLine($"refgen: generated {ordered.Count} type(s) -> '{options.Output}' (fn: {options.RegisterFn})");
	}

	private readonly List<string> m_messages = [];

	private void CheckDirectories()
	{
		foreach (var input in options.Inputs.Where(input => !Directory.Exists(input))) {
			throw new GeneratorException($"--input '{input}' is not a directory");
		}
		if (!Directory.Exists(options.Output)) {
			throw new GeneratorException($"--output '{options.Output}' is not a directory");
		}
	}

	private (List<NodeType> Nodes, List<EventType> Events) ParseHeaders(IEnumerable<string> headers, Action<string> step)
	{
		var nodes = new List<NodeType>();
		var events = new List<EventType>();

		foreach (var header in headers) {
			var includePath = IncludePath(header);
			step(includePath);

			string source;
			try {
				source = File.ReadAllText(header);
			} catch (Exception e) when (e is IOException or UnauthorizedAccessException) {
				m_messages.Add($"error reading '{header}': {e.Message}");
				continue;
			}

			var text = ExportMacros.Strip(source);

			foreach (var node in NodeParser.Parse(text, includePath)) {
				Validation.Check(node);
				nodes.Add(node with { Attributes = options.Attributes.Aggregate(node.Attributes, (list, name) => list.WithMarker(name)) });
			}
			events.AddRange(EventParser.Parse(text, includePath));
		}

		return (nodes, DistinctEvents(events));
	}

	// the same event can be seen from several headers
	private List<EventType> DistinctEvents(IEnumerable<EventType> events)
	{
		var distinct = events
			.OrderBy(e => e.QualifiedName, StringComparer.Ordinal)
			.GroupBy(e => e.QualifiedName)
			.Select(group => group.First())
			.ToList();

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

	private void WriteOrWarn(string path, string content)
	{
		try {
			OutputFile.WriteIfChanged(path, content);
		} catch (GeneratorException e) {
			Warn(e.Message);
		}
	}

	private void Warn(string message) => m_messages.Add($"warning: {message}");
}
