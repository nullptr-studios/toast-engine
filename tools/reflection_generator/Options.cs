namespace ReflectionGenerator;

internal sealed class Options
{
	public const string Usage = """
		usage: reflection_generator --database <file> --output <dir> --input <dir>... [options]

		  --database <file>          json metadata output
		  --output <dir>             where generated c++ is written
		  --input <dir>              folder searched for headers, repeatable
		  --include-root <dir>       headers are included relative to this folder
		  --register-fn <name>       registration function name, default registerEngineTypes
		  --split-typeinfo           emit type_info into one cpp per node
		  --attribute <name>         attribute added to every node, repeatable
		  --lua-stubs <file>         lua types for the reflected nodes
		  --event-lua-stubs <file>   lua types for the events
		""";

	public string Database { get; private set; } = "";
	public string Output { get; private set; } = "";
	public List<string> Inputs { get; } = [];
	public string? IncludeRoot { get; private set; }
	public string RegisterFn { get; private set; } = "registerEngineTypes";
	public bool SplitTypeInfo { get; private set; }
	public List<string> Attributes { get; } = [];
	public string? LuaStubs { get; private set; }
	public string? EventLuaStubs { get; private set; }

	public static Options Parse(IReadOnlyList<string> args)
	{
		var options = new Options();
		for (var i = 0; i < args.Count; i++) {
			var (flag, inlineValue) = SplitFlag(args[i]);

			string Value() => inlineValue ?? (++i < args.Count ? args[i] : throw new GeneratorException($"{flag} needs a value"));

			switch (flag) {
				case "--database": options.Database = Value(); break;
				case "--output": options.Output = Value(); break;
				case "--input": options.Inputs.Add(Value()); break;
				case "--include-root": options.IncludeRoot = Value(); break;
				case "--register-fn": options.RegisterFn = Value(); break;
				case "--split-typeinfo": options.SplitTypeInfo = true; break;
				case "--attribute": options.Attributes.Add(Value()); break;
				case "--lua-stubs": options.LuaStubs = Value(); break;
				case "--event-lua-stubs": options.EventLuaStubs = Value(); break;
				default: throw new GeneratorException($"unknown argument '{args[i]}'\n{Usage}");
			}
		}

		if (options.Database.Length == 0) throw new GeneratorException($"--database is required\n{Usage}");
		if (options.Output.Length == 0) throw new GeneratorException($"--output is required\n{Usage}");
		return options;
	}

	private static (string Flag, string? Value) SplitFlag(string arg)
	{
		var eq = arg.IndexOf('=');
		return arg.StartsWith("--", StringComparison.Ordinal) && eq > 0 ? (arg[..eq], arg[(eq + 1)..]) : (arg, null);
	}
}
