using System;
using System.Collections.Generic;
using System.Globalization;
using Avalonia;
using Avalonia.Media;
using editor.LuaEditor.Layout;

namespace editor.LuaEditor.Canvas;

public sealed class CanvasTheme {
	public static CanvasTheme Default { get; } = new();

	private readonly Dictionary<(string Text, double Size, uint Color, bool Bold), FormattedText> m_text = [];

	public IBrush Background { get; } = Brush.Parse("#121111");
	public IBrush GridMinor { get; } = Brush.Parse("#181717");
	public IBrush GridMajor { get; } = Brush.Parse("#1f1d1e");
	public IBrush NodeBody { get; } = Brush.Parse("#2b2a2a");
	public IBrush NodeBodyDim { get; } = Brush.Parse("#222121");
	public IBrush Text { get; } = Brush.Parse("#fafafa");
	public IBrush TextMuted { get; } = Brush.Parse("#a0a0a0");
	public IBrush Accent { get; } = Brush.Parse("#ff1659");
	public IBrush Error { get; } = Brush.Parse("#ff1659");
	public IBrush Warning { get; } = Brush.Parse("#ffa300");
	public IBrush Exec { get; } = Brush.Parse(Editing.PinStyle.Exec);
	public IBrush ConstantBox { get; } = Brush.Parse("#161515");
	public Pen NodeBorder { get; } = new(Brush.Parse("#383637"), 1);
	public Pen SelectedBorder { get; } = new(Brush.Parse("#ff1659"), 2);
	public Pen ErrorBorder { get; } = new(Brush.Parse("#ff1659"), 2);
	public Pen WarningBorder { get; } = new(Brush.Parse("#ffa300"), 2);
	public Pen ExecWire { get; } = new(Brush.Parse("#fafafa"), 2.5, lineCap: PenLineCap.Round);
	public Typeface Font { get; } = new("Inter, Segoe UI, sans-serif");
	public Typeface FontBold { get; } = new("Inter, Segoe UI, sans-serif", FontStyle.Normal, FontWeight.SemiBold);

	public IBrush HeaderOf(NodeLook look) => look switch {
		NodeLook.Event => Brush.Parse("#ff1659"),
		NodeLook.Flow => Brush.Parse("#454343"),
		NodeLook.Call => Brush.Parse("#29adff"),
		NodeLook.Variable => Brush.Parse("#00d437"),
		NodeLook.Operator => Brush.Parse("#ad73ff"),
		NodeLook.Value => Brush.Parse("#73ffea"),
		NodeLook.Comment => Brush.Parse("#ffec27"),
		_ => Brush.Parse("#ffa300")
	};

	public IBrush HeaderTextOf(NodeLook look) => look is NodeLook.Flow or NodeLook.Event ? Text : Brush.Parse("#121111");

	public static Color ParseColor(string hex) => Color.Parse(hex);

	public Pen WirePen(string color, bool dim = false, double width = 2) => new(new SolidColorBrush(Color.Parse(color), dim ? 0.35 : 0.95), width, lineCap: PenLineCap.Round);

	public FormattedText Format(string text, double size, IBrush brush, bool bold = false) {
		var color = brush is ISolidColorBrush solid ? solid.Color.ToUInt32() : 0u;
		var key = (text, size, color, bold);
		if (m_text.TryGetValue(key, out var cached)) return cached;
		if (m_text.Count > 4000) m_text.Clear();
		var formatted = new FormattedText(text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, bold ? FontBold : Font, size, brush);
		m_text[key] = formatted;
		return formatted;
	}
}
