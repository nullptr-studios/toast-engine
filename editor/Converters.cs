//
// Converters.cs by Xein
// 14 May 2026
//

using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using Avalonia;
using Avalonia.Data.Converters;
using Avalonia.Media;
using Avalonia.Styling;

// ReSharper disable once CheckNamespace
namespace editor.Converters;

public class SeverityToColorConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		if (value is uint severity) {
			var key = severity switch {
				0 => "TextMuted", // Trace
				1 => "Green",     // Info
				2 => "Orange",    // Warning
				_ => "Red"        // Error / Critical
			};
			return ConverterHelpers.GetBrush(key) ?? Brushes.White;
		}

		return Brushes.White;
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

public class SeverityBarColorConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		if (value is uint severity) {
			if (severity == 0) return Brushes.Transparent;
			var key = severity switch {
				1 => "Green",
				2 => "Orange",
				_ => "Red"
			};
			return ConverterHelpers.GetBrush(key) ?? Brushes.White;
		}

		return Brushes.White;
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

public class SeverityRowGradientConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		if (value is not uint severity) return Brushes.Transparent;

		var colorKey = severity switch {
			2 => "DarkOrange",
			>= 3 => "DarkRed", // Error and Critical
			_ => null
		};

		if (colorKey is null || ConverterHelpers.GetBrush(colorKey) is not ISolidColorBrush { Color: var color })
			return Brushes.Transparent;

		return new LinearGradientBrush {
			StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
			EndPoint = new RelativePoint(1, 0, RelativeUnit.Relative),
			GradientStops = {
				new GradientStop(color, 0.0),
				new GradientStop(Color.FromArgb(0, color.R, color.G, color.B), 1.0)
			}
		};
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

public class SeverityFillConverter : IMultiValueConverter {
	public object Convert(IList<object?> values, Type targetType, object? parameter, CultureInfo culture) {
		if (values.Count >= 2 && values[0] is bool enabled && values[1] is uint severity) {
			if (!enabled) return ConverterHelpers.GetBrush("Bg4") ?? Brushes.Transparent;
			var key = severity switch {
				0 => "Blue",   // Trace
				1 => "Green",  // Info
				2 => "Orange", // Warning
				_ => "Red"     // Error / Critical
			};
			return ConverterHelpers.GetBrush(key) ?? Brushes.White;
		}

		return Brushes.Transparent;
	}
}

public class ToggleForegroundConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		if (value is bool b) return b ? Brushes.Black : ConverterHelpers.GetBrush("Text") ?? Brushes.White;
		return Brushes.White;
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

public class BoolFillConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		if (value is bool b && b && parameter is string key)
			return ConverterHelpers.GetBrush(key) ?? Brushes.White;
		return ConverterHelpers.GetBrush("Bg4") ?? Brushes.Transparent;
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

public class TraceForegroundConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		var key = value is uint severity && severity == 0 ? "TextMuted" : "Text";
		return ConverterHelpers.GetBrush(key) ?? Brushes.White;
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

public class BoolToOpacityConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		if (value is bool b) return b ? 1.0 : 0.5;
		return 1.0;
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

public class EnabledToColorConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		if (value is bool b)
			return ConverterHelpers.GetBrush(b ? "Text" : "TextMuted") ?? Brushes.White;
		return Brushes.White;
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

public class ScaleConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		if (value is double d && parameter is string s &&
		    double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out var factor))
			return d * factor;
		return value ?? AvaloniaProperty.UnsetValue;
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

// checks a bound enum value against the string in ConverterParameter
public class EnumEqualsConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		return value?.ToString() == parameter?.ToString();
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

public class SnapValueConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		if (value is not double d) return value ?? AvaloniaProperty.UnsetValue;
		if (parameter is string s && s == "int")
			return ((int)Math.Round(d)).ToString(CultureInfo.InvariantCulture);
		if (d < 1) return d.ToString("0.00", CultureInfo.InvariantCulture)[1..];
		var text = d.ToString("0.#", CultureInfo.InvariantCulture);
		return text.Contains('.') ? text : text + ".";
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

public class DockContentCornerRadiusConverter : IMultiValueConverter {
	public object Convert(IList<object?> values, Type targetType, object? parameter, CultureInfo culture) {
		if (values.Count >= 2 && values[0] is { } active && values[1] is IEnumerable visible) {
			var items = visible.GetEnumerator();
			try {
				if (items.MoveNext() && ReferenceEquals(active, items.Current))
					return new CornerRadius(0, 8, 8, 8);
			} finally {
				(items as IDisposable)?.Dispose();
			}
		}

		return new CornerRadius(8);
	}
}

public class DockFocusBrushConverter : IValueConverter {
	public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture) {
		return ConverterHelpers.GetBrush(value is true ? "Red" : "Bg5") ?? Brushes.Transparent;
	}

	public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

file static class ConverterHelpers {
	internal static IBrush? GetBrush(string key) {
		if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true)
			return r as IBrush;
		return null;
	}
}
