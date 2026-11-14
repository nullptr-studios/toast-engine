using ReflectionGenerator.Model;

namespace ReflectionGenerator.Output;

internal sealed class CppOutput(Templates templates, string outputDir, string registerFn, bool splitTypeInfo)
{
	public void Write(IEnumerable<NodeType> nodes, IEnumerable<EventType> events)
	{
		var views = nodes.Select(n => NodeView.From(n, splitTypeInfo)).ToList();

		foreach (var node in views) {
			Emit($"{node.Slug}.generated.hpp", templates.Render("node.generated.hpp.scriban", "node", node));
			if (splitTypeInfo) {
				Emit($"{node.Slug}.type_info.cpp", templates.Render("node.type_info.cpp.scriban", "node", node));
			}
		}

		var eventViews = events.Where(e => e.Supported).Select(EventView.From).ToList();
		var registry = new RegistryView(views, eventViews, registerFn, splitTypeInfo);
		Emit("reflect.generated.cpp", templates.Render("reflect.generated.cpp.scriban", "registry", registry));
	}

	private void Emit(string fileName, string content) => OutputFile.WriteIfChanged(Path.Combine(outputDir, fileName), content);
}
