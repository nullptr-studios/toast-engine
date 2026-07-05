using System;
using System.Globalization;
using Avalonia;
using Avalonia.Data.Converters;
using Avalonia.Media;
using Avalonia.Styling;

namespace editor.Components.Converters;

public sealed class ResourceBrushConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		if (value is string key && Application.Current is { } app
		    && app.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) && r is IBrush b)
			return b;
		return Brushes.Transparent;
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}
