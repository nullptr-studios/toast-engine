//
// ReverseLogList.cs by Xein
// 28 Jul 2026
//

using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;

namespace editor.Logger;

public sealed class ReverseLogList : IList, IReadOnlyList<LogEntry>, INotifyCollectionChanged {
	private List<LogEntry> m_items = []; // oldest to newest

	public int Count => m_items.Count;

	IEnumerator IEnumerable.GetEnumerator() {
		return GetEnumerator();
	}

	object? IList.this[int index] {
		get => this[index];
		set => throw new NotSupportedException();
	}

	bool IList.IsFixedSize => false;
	bool IList.IsReadOnly => true;
	bool ICollection.IsSynchronized => false;
	object ICollection.SyncRoot => this;

	int IList.Add(object? value) {
		throw new NotSupportedException();
	}

	void IList.Clear() {
		throw new NotSupportedException();
	}

	void IList.Insert(int index, object? value) {
		throw new NotSupportedException();
	}

	void IList.Remove(object? value) {
		throw new NotSupportedException();
	}

	void IList.RemoveAt(int index) {
		throw new NotSupportedException();
	}

	bool IList.Contains(object? value) {
		return value is LogEntry e && m_items.Contains(e);
	}

	int IList.IndexOf(object? value) {
		if (value is not LogEntry e) return -1;
		var i = m_items.LastIndexOf(e);
		return i < 0 ? -1 : m_items.Count - 1 - i;
	}

	void ICollection.CopyTo(Array array, int index) {
		for (var i = 0; i < Count; i++) array.SetValue(this[i], index + i);
	}

	public event NotifyCollectionChangedEventHandler? CollectionChanged;

	public LogEntry this[int index] => m_items[m_items.Count - 1 - index];

	public IEnumerator<LogEntry> GetEnumerator() {
		for (var i = m_items.Count - 1; i >= 0; i--) yield return m_items[i];
	}

	public void ResetTo(List<LogEntry> oldestFirst) {
		m_items = oldestFirst;
		CollectionChanged?.Invoke(this, new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
	}
}
