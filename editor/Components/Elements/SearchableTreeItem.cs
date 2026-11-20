using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Collections;
using editor.Components.Modals;

namespace editor.Components.Elements;

/// <summary>
///    Base for tree nodes with text filtering — <see cref="UpdateFilter" /> recurses down and returns true if
///    anything in the subtree matched
/// </summary>
public abstract class SearchableTreeItem<T> : IPickerNode where T : SearchableTreeItem<T> {
	public bool IsExpanded { get; set; } = true;
	IEnumerable IPickerNode.VisibleChildren => FilteredChildren;
	public string Name { get; protected init; } = "";
	public List<T> AllChildren { get; protected init; } = [];
	public ObservableCollection<T> FilteredChildren { get; } = [];
	public ObservableCollection<TextSegment> Segments { get; } = [];

	protected void InitSegments() {
		Segments.Add(new TextSegment(Name, false));
	}

	public bool UpdateFilter(string query) {
		if (string.IsNullOrWhiteSpace(query)) {
			FilteredChildren.Clear();
			foreach (var c in AllChildren) {
				c.UpdateFilter(query);
				FilteredChildren.Add(c);
			}

			Segments.Clear();
			Segments.Add(new TextSegment(Name, false));
			return true;
		}

		var own = FuzzyMatcher.Segments(Name, query);
		Segments.Clear();
		foreach (var segment in own ?? [new TextSegment(Name, false)]) Segments.Add(segment);

		var visible = AllChildren.Where(c => c.UpdateFilter(query)).ToList();
		FilteredChildren.Clear();
		foreach (var c in visible) FilteredChildren.Add(c);

		return own is not null || visible.Count > 0;
	}
}
