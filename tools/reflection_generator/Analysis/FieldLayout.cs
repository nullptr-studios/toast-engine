using ReflectionGenerator.Model;

namespace ReflectionGenerator.Analysis;

internal sealed record FieldSubgroup(string Name, IReadOnlyList<Field> Fields);

internal sealed record FieldGroup(string Name, IReadOnlyList<Field> Fields, IReadOnlyList<FieldSubgroup> Subgroups);

// the inspector shows loose fields first then each Group with its Subgroups
internal sealed record FieldLayout(IReadOnlyList<Field> GlobalFields, IReadOnlyList<FieldGroup> Groups)
{
	// generated indices and lua stubs both follow this order
	public IEnumerable<Field> InOrder => GlobalFields.Concat(Groups.SelectMany(g => g.Fields.Concat(g.Subgroups.SelectMany(s => s.Fields))));

	public static FieldLayout From(IEnumerable<Field> fields)
	{
		var global = new List<Field>();
		var groups = new SortedDictionary<string, GroupBuilder>(StringComparer.Ordinal);

		foreach (var field in fields) {
			var group = field.Attributes.FirstArg("Group");
			if (group is null) {
				global.Add(field);
				continue;
			}

			if (!groups.TryGetValue(group, out var builder)) {
				groups[group] = builder = new GroupBuilder();
			}
			builder.Add(field, field.Attributes.FirstArg("Subgroup"));
		}

		return new FieldLayout(global, groups.Select(g => g.Value.Build(g.Key)).ToList());
	}

	private sealed class GroupBuilder
	{
		private readonly List<Field> m_fields = [];
		private readonly SortedDictionary<string, List<Field>> m_subgroups = new(StringComparer.Ordinal);

		public void Add(Field field, string? subgroup)
		{
			if (subgroup is null) {
				m_fields.Add(field);
				return;
			}

			if (!m_subgroups.TryGetValue(subgroup, out var list)) {
				m_subgroups[subgroup] = list = [];
			}
			list.Add(field);
		}

		public FieldGroup Build(string name) =>
			new(name, m_fields, m_subgroups.Select(s => new FieldSubgroup(s.Key, s.Value)).ToList());
	}
}
