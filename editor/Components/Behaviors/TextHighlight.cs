using System;
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

		if (string.IsNullOrEmpty(query) || string.IsNullOrEmpty(text)) {
			tb.Inlines.Add(new Run(text));
			return;
		}

		var red = ResolveRed();
		var i = 0;
		while (i < text.Length) {
			var m = text.IndexOf(query, i, StringComparison.OrdinalIgnoreCase);
			if (m < 0) {
				tb.Inlines.Add(new Run(text[i..]));
				break;
			}

			if (m > i) tb.Inlines.Add(new Run(text[i..m]));
			tb.Inlines.Add(new Run(text.Substring(m, query.Length)) {
				Foreground = red,
				FontWeight = FontWeight.Bold
			});
			i = m + query.Length;
		}
	}

	private static IBrush ResolveRed() {
		if (Application.Current is { } app &&
		    app.TryGetResource("Red", app.ActualThemeVariant, out var v) && v is IBrush b)
			return b;
		return new SolidColorBrush(Color.Parse("#ff1659"));
	}
}
