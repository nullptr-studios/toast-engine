using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Media;

namespace editor.Components.Behaviors;

public static class TextHighlight {
	public static readonly AttachedProperty<string?> TextProperty =
		AvaloniaProperty.RegisterAttached<TextBlock, string?>("Text", typeof(TextHighlight));

	public static readonly AttachedProperty<string?> QueryProperty =
		AvaloniaProperty.RegisterAttached<TextBlock, string?>("Query", typeof(TextHighlight));

	static TextHighlight() {
		TextProperty.Changed.AddClassHandler<TextBlock>((tb, _) => Apply(tb));
		QueryProperty.Changed.AddClassHandler<TextBlock>((tb, _) => Apply(tb));
	}

	public static void SetText(TextBlock o, string? v) {
		o.SetValue(TextProperty, v);
	}

	public static string? GetText(TextBlock o) {
		return o.GetValue(TextProperty);
	}

	public static void SetQuery(TextBlock o, string? v) {
		o.SetValue(QueryProperty, v);
	}

	public static string? GetQuery(TextBlock o) {
		return o.GetValue(QueryProperty);
	}

	private static void Apply(TextBlock tb) {
		var text = GetText(tb) ?? "";
		var query = GetQuery(tb) ?? "";

		tb.Inlines ??= new InlineCollection();
		tb.Inlines.Clear();

		var tokens = query.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
		if (tokens.Length == 0 || string.IsNullOrEmpty(text)) {
			tb.Inlines.Add(new Run(text));
			return;
		}

		var ranges = new List<(int Start, int End)>();
		foreach (var token in tokens) {
			var i = 0;
			while (i < text.Length) {
				var m = text.IndexOf(token, i, StringComparison.OrdinalIgnoreCase);
				if (m < 0) break;
				ranges.Add((m, m + token.Length));
				i = m + token.Length;
			}
		}

		if (ranges.Count == 0) {
			tb.Inlines.Add(new Run(text));
			return;
		}

		ranges.Sort((a, b) => a.Start.CompareTo(b.Start));
		var merged = new List<(int Start, int End)>();
		foreach (var r in ranges)
			if (merged.Count > 0 && r.Start <= merged[^1].End) {
				var last = merged[^1];
				merged[^1] = (last.Start, Math.Max(last.End, r.End));
			} else {
				merged.Add(r);
			}

		var red = ResolveRed();
		var pos = 0;
		foreach (var (start, end) in merged) {
			if (start > pos) tb.Inlines.Add(new Run(text[pos..start]));
			tb.Inlines.Add(new Run(text[start..end]) {
				Foreground = red,
				FontWeight = FontWeight.Bold
			});
			pos = end;
		}

		if (pos < text.Length) tb.Inlines.Add(new Run(text[pos..]));
	}

	private static IBrush ResolveRed() {
		if (Application.Current is { } app &&
		    app.TryGetResource("Red", app.ActualThemeVariant, out var v) && v is IBrush b)
			return b;
		return new SolidColorBrush(Color.Parse("#ff1659"));
	}
}
