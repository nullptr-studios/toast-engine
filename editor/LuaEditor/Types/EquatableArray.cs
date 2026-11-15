using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;

namespace editor.LuaEditor.Types;

// type records need collection equality by value
[CollectionBuilder(typeof(EquatableArray), nameof(EquatableArray.Create))]
public readonly struct EquatableArray<T> : IReadOnlyList<T>, IEquatable<EquatableArray<T>> {
	private readonly T[]? m_items;

	public EquatableArray(T[] items) {
		m_items = items;
	}

	private T[] Items => m_items ?? [];

	public int Count => Items.Length;
	public T this[int index] => Items[index];

	public static implicit operator EquatableArray<T>(T[] items) => new(items);
	public static implicit operator EquatableArray<T>(List<T> items) => new(items.ToArray());

	public bool Equals(EquatableArray<T> other) => Items.AsSpan().SequenceEqual(other.Items, EqualityComparer<T>.Default);
	public override bool Equals(object? obj) => obj is EquatableArray<T> other && Equals(other);

	public override int GetHashCode() {
		var hash = new HashCode();
		foreach (var item in Items) hash.Add(item);
		return hash.ToHashCode();
	}

	public static bool operator ==(EquatableArray<T> a, EquatableArray<T> b) => a.Equals(b);
	public static bool operator !=(EquatableArray<T> a, EquatableArray<T> b) => !a.Equals(b);

	public IEnumerator<T> GetEnumerator() => ((IEnumerable<T>)Items).GetEnumerator();
	IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

	public override string ToString() => $"[{string.Join(", ", Items.Select(i => i?.ToString()))}]";
}

public static class EquatableArray {
	public static EquatableArray<T> Create<T>(ReadOnlySpan<T> items) => new(items.ToArray());
}
