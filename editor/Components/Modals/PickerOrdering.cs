using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace editor.Components.Modals;

internal static class PickerOrdering {
	public static int CompareNames(string left, string right) {
		var result = StringComparer.OrdinalIgnoreCase.Compare(left, right);
		return result != 0 ? result : StringComparer.Ordinal.Compare(left, right);
	}

	public static IOrderedEnumerable<T> ByName<T>(IEnumerable<T> items, Func<T, string> name) {
		return items.OrderBy(name, StringComparer.OrdinalIgnoreCase)
			.ThenBy(name, StringComparer.Ordinal);
	}

	public static void InsertByName<T>(ObservableCollection<T> items, T item, Func<T, string> name) {
		var index = 0;
		while (index < items.Count && CompareNames(name(items[index]), name(item)) <= 0) index++;
		items.Insert(index, item);
	}

	public static int CompareFolderFirst(bool leftFolder, string leftName, bool rightFolder, string rightName) {
		if (leftFolder != rightFolder) return leftFolder ? -1 : 1;
		return CompareNames(leftName, rightName);
	}

	public static void InsertFolderFirst<T>(
		ObservableCollection<T> items, T item,
		Func<T, bool> isFolder, Func<T, string> name) {
		var index = 0;
		while (index < items.Count &&
		       CompareFolderFirst(isFolder(items[index]), name(items[index]), isFolder(item), name(item)) <= 0)
			index++;
		items.Insert(index, item);
	}
}
