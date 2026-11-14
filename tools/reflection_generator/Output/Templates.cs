using System.Reflection;
using Scriban;
using Scriban.Parsing;
using Scriban.Runtime;
using Scriban.Syntax;

namespace ReflectionGenerator.Output;

internal sealed class Templates
{
	private readonly Dictionary<string, Template> m_parsed = [];

	public string Render(string file, string variable, object model)
	{
		var context = new TemplateContext {
			TemplateLoader = new EmbeddedLoader(),
			// a typo in a template must fail loudly instead of rendering nothing
			StrictVariables = true,
			EnableRelaxedMemberAccess = false,
		};
		context.PushGlobal(new ScriptObject { [variable] = model });

		try {
			return Parse(file).Render(context).Replace("\r\n", "\n");
		} catch (ScriptRuntimeException e) {
			throw new GeneratorException($"template {file}: {e.Message}");
		}
	}

	private Template Parse(string file)
	{
		if (m_parsed.TryGetValue(file, out var cached)) return cached;

		var template = Template.Parse(EmbeddedLoader.Read(file), file);
		if (template.HasErrors) {
			throw new GeneratorException($"template {file}: {string.Join("; ", template.Messages)}");
		}
		return m_parsed[file] = template;
	}

	private sealed class EmbeddedLoader : ITemplateLoader
	{
		public static string Read(string name)
		{
			using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name)
				?? throw new GeneratorException($"template '{name}' is not embedded");
			using var reader = new StreamReader(stream);
			return reader.ReadToEnd().Replace("\r\n", "\n");
		}

		public string GetPath(TemplateContext context, SourceSpan callerSpan, string templateName) => templateName;

		public string Load(TemplateContext context, SourceSpan callerSpan, string templatePath) => Read(templatePath);

		public ValueTask<string?> LoadAsync(TemplateContext context, SourceSpan callerSpan, string templatePath) =>
			new((string?)Read(templatePath));
	}
}
