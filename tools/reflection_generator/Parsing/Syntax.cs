using TreeSitter;

namespace ReflectionGenerator.Parsing;

// the only file that knows how tree-sitter is shaped
internal static class Syntax
{
	private static readonly Language Cpp = new("cpp");

	public static Tree Parse(string source)
	{
		using var parser = new Parser(Cpp);
		return parser.Parse(source) ?? throw new GeneratorException("tree-sitter could not parse the source");
	}

	public static string Str(this Node node) => node.Text ?? "";

	public static Node? Field(this Node node, string name) => node.GetChildForField(name);

	public static IEnumerable<Node> Descendants(this Node node)
	{
		foreach (var child in node.Children) {
			yield return child;
			foreach (var inner in child.Descendants()) {
				yield return inner;
			}
		}
	}

	public static IEnumerable<Node> DescendantsOfKind(this Node node, string kind) =>
		node.Descendants().Where(n => n.Type == kind);

	public static IEnumerable<Node> ChildrenOfKind(this Node node, string kind) =>
		node.Children.Where(n => n.Type == kind);

	public static string Slice(this Node node, int start, int end) => node.Tree.Source[start..end];

	public static string? EnclosingNamespace(this Node node)
	{
		for (var parent = node.Parent; parent is not null; parent = parent.Parent) {
			if (parent.Type == "namespace_definition" && parent.Field("name") is { } name) {
				return name.Str();
			}
		}
		return null;
	}
}
