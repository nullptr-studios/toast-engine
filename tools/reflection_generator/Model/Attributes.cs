using System.Collections;

namespace ReflectionGenerator.Model;

internal sealed record CppAttribute(string Name, IReadOnlyList<string> Args);

internal sealed class AttributeList(IEnumerable<CppAttribute> items) : IEnumerable<CppAttribute>
{
	private readonly List<CppAttribute> m_items = items.ToList();

	public static AttributeList Empty => new([]);

	public bool Has(string name) => m_items.Any(a => a.Name == name);

	public string? FirstArg(string name) => m_items.FirstOrDefault(a => a.Name == name)?.Args.FirstOrDefault();

	public AttributeList Without(string name) => new(m_items.Where(a => a.Name != name));

	public AttributeList WithMarker(string name) => Has(name) ? this : new(m_items.Append(new CppAttribute(name, [])));

	// generated code and json both want attributes sorted by name with the last repeat winning
	public SortedDictionary<string, string[]> ByName()
	{
		var result = new SortedDictionary<string, string[]>(StringComparer.Ordinal);
		foreach (var attribute in m_items) {
			result[attribute.Name] = attribute.Args.ToArray();
		}
		return result;
	}

	public IEnumerator<CppAttribute> GetEnumerator() => m_items.GetEnumerator();

	IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
