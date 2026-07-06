//
// Converters.cs by Xein
// 14 May 2026
//

using System;
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
				2 => "Yellow",    // Warning
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
		var text = d.ToString(d < 1 ? "0.00" : "0.0", CultureInfo.InvariantCulture);
		return d < 1 ? text[1..] : text;
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
