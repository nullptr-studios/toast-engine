using System;
using System.Collections.Generic;

namespace editor.Components.Elements;

public static class FuzzyMatcher {
	public static bool Matches(string text, string query) {
		return Match(text, query) is not null;
	}

	public static bool[]? Match(string text, string query) {
		var mask = new bool[text.Length];
		var raw = query.Trim();
		if (raw.Length == 0) return mask;

		if (Mark(mask, text.IndexOf(raw, StringComparison.OrdinalIgnoreCase), raw.Length)) return mask;

		var compact = raw.Replace(" ", "").Replace("\t", "");
		if (compact.Length == 0) return mask;
		if (Mark(mask, text.IndexOf(compact, StringComparison.OrdinalIgnoreCase), compact.Length)) return mask;

		var end = -1;
		var next = 0;
		for (var i = 0; i < text.Length && next < compact.Length; i++)
			if (Same(text[i], compact[next])) {
				next++;
				end = i;
			}

		if (next < compact.Length) return null;

		var back = compact.Length - 1;
		for (var i = end; i >= 0 && back >= 0; i--)
			if (Same(text[i], compact[back])) {
				mask[i] = true;
				back--;
			}

		return mask;
	}

	public static List<TextSegment>? Segments(string text, string query) {
		if (Match(text, query) is not { } mask) return null;
		return Split(text, mask);
	}

	public static List<TextSegment> Split(string text, bool[] mask) {
		var segments = new List<TextSegment>(3);
		var start = 0;
		for (var i = 1; i <= text.Length; i++) {
			if (i < text.Length && mask[i] == mask[start]) continue;
			segments.Add(new TextSegment(text[start..i], mask[start]));
			start = i;
		}

		if (segments.Count == 0) segments.Add(new TextSegment(text, false));
		return segments;
	}

	private static bool Mark(bool[] mask, int at, int length) {
		if (at < 0) return false;
		for (var i = at; i < at + length; i++) mask[i] = true;
		return true;
	}

	private static bool Same(char a, char b) {
		return char.ToLowerInvariant(a) == char.ToLowerInvariant(b);
	}
}
