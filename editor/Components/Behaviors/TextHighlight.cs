using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Media;
using editor.Components.Elements;

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

		if (string.IsNullOrWhiteSpace(query) || string.IsNullOrEmpty(text) || FuzzyMatcher.Segments(text, query) is not { } segments) {
			tb.Inlines.Add(new Run(text));
			return;
		}

		var red = ResolveRed();
		foreach (var segment in segments)
			tb.Inlines.Add(segment.IsHighlighted
				? new Run(segment.Text) { Foreground = red, FontWeight = FontWeight.Bold }
				: new Run(segment.Text));
	}

	private static IBrush ResolveRed() {
		if (Application.Current is { } app &&
		    app.TryGetResource("Red", app.ActualThemeVariant, out var v) && v is IBrush b)
			return b;
		return new SolidColorBrush(Color.Parse("#ff1659"));
	}
}
