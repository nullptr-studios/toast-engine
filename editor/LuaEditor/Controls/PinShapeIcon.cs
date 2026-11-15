using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using editor.LuaEditor.Editing;

namespace editor.LuaEditor.Controls;

public static class PinGlyph {
	public static void Draw(DrawingContext context, Point p, PinShape shape, Color color, double r = 5.5, bool filled = true) {
		var brush = new SolidColorBrush(color);
		var pen = new Pen(brush, 1.8);
		IBrush? fill = filled ? brush : null;
		switch (shape) {
			case PinShape.Diamond: {
				var g = new StreamGeometry();
				using (var c = g.Open()) {
					c.BeginFigure(new Point(p.X, p.Y - r - 1), true);
					c.LineTo(new Point(p.X + r + 1, p.Y));
					c.LineTo(new Point(p.X, p.Y + r + 1));
					c.LineTo(new Point(p.X - r - 1, p.Y));
					c.EndFigure(true);
				}
				context.DrawGeometry(fill, pen, g);
				break;
			}
			case PinShape.Triangle: {
				var g = new StreamGeometry();
				using (var c = g.Open()) {
					c.BeginFigure(new Point(p.X - r, p.Y - r - 1), true);
					c.LineTo(new Point(p.X + r + 2, p.Y));
					c.LineTo(new Point(p.X - r, p.Y + r + 1));
					c.EndFigure(true);
				}
				context.DrawGeometry(fill, pen, g);
				break;
			}
			case PinShape.Hexagon: {
				var g = new StreamGeometry();
				var h = r + 1.5;
				using (var c = g.Open()) {
					c.BeginFigure(new Point(p.X - h, p.Y), true);
					c.LineTo(new Point(p.X - h / 2, p.Y - h * 0.87));
					c.LineTo(new Point(p.X + h / 2, p.Y - h * 0.87));
					c.LineTo(new Point(p.X + h, p.Y));
					c.LineTo(new Point(p.X + h / 2, p.Y + h * 0.87));
					c.LineTo(new Point(p.X - h / 2, p.Y + h * 0.87));
					c.EndFigure(true);
				}
				context.DrawGeometry(fill, pen, g);
				break;
			}
			case PinShape.Square:
				context.DrawRectangle(fill, pen, new Rect(p.X - r, p.Y - r, r * 2, r * 2), 1.5, 1.5);
				break;
			case PinShape.Pair:
				DrawPair(context, p, color, r * 0.65, filled);
				break;
			case PinShape.Stack: {
				context.DrawRectangle(fill, pen, new Rect(p.X - r, p.Y - r, r * 2, r * 2), 1.5, 1.5);
				var ink = new Pen(filled ? new SolidColorBrush(Color.Parse("#1c1b1b")) : brush, 1.1);
				context.DrawLine(ink, new Point(p.X - r + 1.5, p.Y), new Point(p.X + r - 1.5, p.Y));
				context.DrawLine(ink, new Point(p.X, p.Y - r + 1.5), new Point(p.X, p.Y + r - 1.5));
				break;
			}
			default:
				context.DrawEllipse(fill, pen, p, r, r);
				break;
		}
	}

	public static void DrawPair(DrawingContext context, Point p, Color color, double r = 3.6, bool filled = true) {
		var brush = new SolidColorBrush(color);
		var pen = new Pen(brush, 1.6);
		IBrush? fill = filled ? brush : null;
		context.DrawLine(pen, new Point(p.X - 4, p.Y), new Point(p.X + 4, p.Y));
		context.DrawEllipse(fill, pen, new Point(p.X - 5.5, p.Y), r, r);
		context.DrawRectangle(fill, pen, new Rect(p.X + 2.2, p.Y - r, r * 2, r * 2), 1, 1);
	}
}

public sealed class PinShapeIcon : Control {
	public static readonly StyledProperty<PinShape> ShapeProperty = AvaloniaProperty.Register<PinShapeIcon, PinShape>(nameof(Shape));
	public static readonly StyledProperty<string> ColorProperty = AvaloniaProperty.Register<PinShapeIcon, string>(nameof(Color), "#d8d8d8");
	public static readonly StyledProperty<bool> IsPairProperty = AvaloniaProperty.Register<PinShapeIcon, bool>(nameof(IsPair));
	public static readonly StyledProperty<bool> FilledProperty = AvaloniaProperty.Register<PinShapeIcon, bool>(nameof(Filled), true);

	static PinShapeIcon() {
		AffectsRender<PinShapeIcon>(ShapeProperty, ColorProperty, IsPairProperty, FilledProperty);
	}

	public PinShapeIcon() {
		Width = 18;
		Height = 18;
	}

	public PinShape Shape {
		get => GetValue(ShapeProperty);
		set => SetValue(ShapeProperty, value);
	}

	public string Color {
		get => GetValue(ColorProperty);
		set => SetValue(ColorProperty, value);
	}

	public bool IsPair {
		get => GetValue(IsPairProperty);
		set => SetValue(IsPairProperty, value);
	}

	public bool Filled {
		get => GetValue(FilledProperty);
		set => SetValue(FilledProperty, value);
	}

	public override void Render(DrawingContext context) {
		var center = new Point(Bounds.Width / 2, Bounds.Height / 2);
		var color = Avalonia.Media.Color.TryParse(Color, out var parsed) ? parsed : Avalonia.Media.Color.Parse("#d8d8d8");
		if (IsPair) PinGlyph.DrawPair(context, center, color);
		else PinGlyph.Draw(context, center, Shape, color, 5, Filled);
	}
}
