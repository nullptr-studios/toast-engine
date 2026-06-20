using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace editor.Components.Elements;

/// <summary>
///    Base for tree nodes with text filtering — <see cref="UpdateFilter" /> recurses down and returns true if
///    anything in the subtree matched
/// </summary>
public abstract class SearchableTreeItem<T> where T : SearchableTreeItem<T> {
	public string Name { get; protected init; } = "";
	public List<T> AllChildren { get; protected init; } = [];
	public ObservableCollection<T> FilteredChildren { get; } = [];
	public ObservableCollection<TextSegment> Segments { get; } = [];

	protected void InitSegments() {
		Segments.Add(new TextSegment(Name, false));
	}

	public bool UpdateFilter(string query, bool caseSensitive) {
		if (string.IsNullOrEmpty(query)) {
			FilteredChildren.Clear();
			foreach (var c in AllChildren) FilteredChildren.Add(c);
			Segments.Clear();
			Segments.Add(new TextSegment(Name, false));
			return true;
		}

		var cmp = caseSensitive ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;
		var idx = Name.IndexOf(query, cmp);

		RebuildSegments(idx, query.Length);

		var visible = AllChildren.Where(c => c.UpdateFilter(query, caseSensitive)).ToList();
		FilteredChildren.Clear();
		foreach (var c in visible) FilteredChildren.Add(c);

		return idx >= 0 || visible.Count > 0;
	}

	private void RebuildSegments(int idx, int len) {
		Segments.Clear();
		if (idx < 0) {
			Segments.Add(new TextSegment(Name, false));
			return;
		}

		if (idx > 0) Segments.Add(new TextSegment(Name[..idx], false));
		Segments.Add(new TextSegment(Name.Substring(idx, len), true));
		if (idx + len < Name.Length) Segments.Add(new TextSegment(Name[(idx + len)..], false));
	}
}
