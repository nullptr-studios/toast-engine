//
// EditorColorPalette.cs
// 24 Sep 2026
//

using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Styling;

namespace editor.Components.Elements;

public sealed class EditorColorPalette : IColorPalette {
	public static readonly string[] Hues = ["Red", "Green", "Blue", "Magenta", "Orange", "Yellow", "Cyan", "Beige"];
	private static readonly string[] s_shadePrefixes = ["", "Dark", "Bright"];

	public static EditorColorPalette Instance { get; } = new();

	public int ColorCount => Hues.Length;
	public int ShadeCount => s_shadePrefixes.Length;

	public Color GetColor(int colorIndex, int shadeIndex) {
		return Resolve(s_shadePrefixes[shadeIndex] + Hues[colorIndex]);
	}

	public static Color Resolve(string key) {
		if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true &&
		    r is ISolidColorBrush brush)
			return brush.Color;
		return Colors.Gray;
	}
}
