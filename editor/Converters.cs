//
// Converters.cs by Xein
// 14 May 2026
//

using Avalonia.Data.Converters;
using Avalonia.Media;
using System;
using System.Globalization;

namespace editor.Converters {
	public class SeverityToColorConverter : IValueConverter {
		public object Convert(object value, Type targetType, object parameter, CultureInfo culture) {
			if (value is uint severity) {
				return severity switch {
					0 => Brushes.Gray,             // Trace
					1 => Brushes.LightSeaGreen,    // Info
					2 => Brushes.Yellow,           // Warning
					3 => Brushes.Red,              // Error
					4 => Brushes.Red,              // Critical
					_ => Brushes.White             // Default
				};
			}

			return Brushes.White;
		}

		public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture) {
			throw new NotSupportedException();
		}
	}
}