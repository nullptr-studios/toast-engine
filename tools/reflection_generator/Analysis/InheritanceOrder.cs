using ReflectionGenerator.Model;

namespace ReflectionGenerator.Analysis;

internal static class InheritanceOrder
{
	// a generated header includes its parent's header so parents have to come first
	public static IReadOnlyList<NodeType> Sort(IReadOnlyList<NodeType> nodes)
	{
		var indexByName = new Dictionary<string, int>();
		for (var i = 0; i < nodes.Count; i++) {
			indexByName[nodes[i].QualifiedName] = i;
		}

		var parents = nodes.Select(n => ParentIndex(n, indexByName)).ToList();
		var placed = new bool[nodes.Count];
		var order = new List<int>();

		for (var start = 0; start < nodes.Count; start++) {
			// climb to an ancestor that is already placed or to the start of a cycle then emit top down
			var chain = new List<int>();
			for (int? current = start; current is { } i && !placed[i] && !chain.Contains(i); current = parents[i]) {
				chain.Add(i);
			}

			chain.Reverse();
			foreach (var i in chain) {
				placed[i] = true;
				order.Add(i);
			}
		}

		return order.Select(i => nodes[i]).ToList();
	}

	// looks the parent up from the innermost namespace outwards like c++ does
	private static int? ParentIndex(NodeType node, Dictionary<string, int> indexByName)
	{
		if (node.Parent is null) return null;

		var written = Strings.Qualify(node.Parent.Namespace, node.Parent.Name);
		if (written.StartsWith("::", StringComparison.Ordinal)) {
			return indexByName.TryGetValue(written[2..], out var global) ? global : null;
		}

		var scope = node.Namespace;
		while (true) {
			if (indexByName.TryGetValue(Strings.Qualify(scope, written), out var index)) return index;
			if (scope is null) return null;

			var cut = scope.LastIndexOf("::", StringComparison.Ordinal);
			scope = cut < 0 ? null : scope[..cut];
		}
	}
}
