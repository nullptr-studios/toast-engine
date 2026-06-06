//
// Converters.cs by Xein
// 14 May 2026
//

using System;
using System.Globalization;
using Avalonia.Data.Converters;
using Avalonia.Media;

namespace editor.Converters;

public class SeverityToColorConverter : IValueConverter {
	public object Convert(object value, Type targetType, object parameter, CultureInfo culture) {
		if (value is uint severity)
			return severity switch {
				0 => Brushes.Gray,          // Trace
				1 => Brushes.LightSeaGreen, // Info
				2 => Brushes.Yellow,        // Warning
				3 => Brushes.Red,           // Error
				4 => Brushes.Red,           // Critical
				_ => Brushes.White          // Default
			};

		return Brushes.White;
	}

	public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

public class BoolToOpacityConverter : IValueConverter {
	public object Convert(object value, Type targetType, object parameter, CultureInfo culture) {
		if (value is bool b) return b ? 1.0 : 0.5;
		return 1.0;
	}

	public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}

public class EnabledToColorConverter : IValueConverter {
	public object Convert(object value, Type targetType, object parameter, CultureInfo culture) {
		if (value is bool b) return b ? Brushes.White : Brushes.Gray;
		return Brushes.White;
	}

	public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) {
		throw new NotSupportedException();
	}
}
