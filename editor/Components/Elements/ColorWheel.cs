//
// ColorWheel.cs
// 27 Nov 2026
//

using System;
using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Media;

namespace editor.Components.Elements;

/// Grading wheel for a tint stored as white plus a brightness free offset, the way the engine reads lift, gamma and gain
public class ColorWheel : Control {
	/// Chroma at the rim, in Rec.709 Cb/Cr units
	private const double RimChroma = 0.3;

	private const int RingSegments = 180;
	private const double FineFactor = 0.1;

	/// Dot travel per pixel of pointer travel
	private const double DragFactor = 0.2;

	public static readonly StyledProperty<float> RProperty =
		AvaloniaProperty.Register<ColorWheel, float>(nameof(R), 1f, defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<float> GProperty =
		AvaloniaProperty.Register<ColorWheel, float>(nameof(G), 1f, defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<float> BProperty =
		AvaloniaProperty.Register<ColorWheel, float>(nameof(B), 1f, defaultBindingMode: BindingMode.TwoWay);

	private Point m_lastPointer;
	private bool m_dragging;

	static ColorWheel() {
		AffectsRender<ColorWheel>(RProperty, GProperty, BProperty);
		Debug.Assert(RoundTrips(0.1, -0.2) && RoundTrips(-0.3, 0.0) && RoundTrips(0.0, 0.25));
	}

	public ColorWheel() {
		Cursor = new Cursor(StandardCursorType.Hand);
		ToolTip.SetTip(this, "Drag to tint, Shift for fine, double click to reset");
	}

	public float R { get => GetValue(RProperty); set => SetValue(RProperty, value); }
	public float G { get => GetValue(GProperty); set => SetValue(GProperty, value); }
	public float B { get => GetValue(BProperty); set => SetValue(BProperty, value); }

	/// Tint for chroma (cb, cr): white plus the Y = 0 YCbCr colour, so its Rec.709 luminance is exactly 1
	public static (float R, float G, float B) TintFromChroma(double cb, double cr) {
		return ((float)(1.0 + 1.5748 * cr), (float)(1.0 - 0.1873 * cb - 0.4681 * cr), (float)(1.0 + 1.8556 * cb));
	}

	/// Inverse of TintFromChroma; brightness a tint carries is ignored, as the engine ignores it
	public static (double Cb, double Cr) ChromaFromTint(float r, float g, float b) {
		var luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
		return ((b - luma) / 1.8556, (r - luma) / 1.5748);
	}

	private static bool RoundTrips(double cb, double cr) {
		var (r, g, b) = TintFromChroma(cb, cr);
		var (cb2, cr2) = ChromaFromTint(r, g, b);
		return Math.Abs(cb - cb2) < 1e-3 && Math.Abs(cr - cr2) < 1e-3;
	}

	protected override Size MeasureOverride(Size availableSize) {
		var side = double.IsInfinity(availableSize.Width) ? 120 : Math.Min(availableSize.Width, 160);
		return new Size(side, side);
	}

	public override void Render(DrawingContext context) {
		var radius = Math.Min(Bounds.Width, Bounds.Height) / 2 - 2;
		if (radius <= 4) return;
		var center = new Point(Bounds.Width / 2, Bounds.Height / 2);
		var ringInner = radius * 0.9;

		context.DrawEllipse(new SolidColorBrush(EditorColorPalette.Resolve("Bg1")), null, center, radius, radius);

		// Each segment shows the colour a dot in that direction gives, so the ring cannot disagree with the mapping
		var thickness = 2 * Math.PI * radius / RingSegments * 1.6;
		for (var i = 0; i < RingSegments; i++) {
			var angle = 2 * Math.PI * i / RingSegments;
			var (dx, dy) = (Math.Cos(angle), -Math.Sin(angle));
			var pen = new Pen(new SolidColorBrush(DisplayColor(dx, -dy, 2.0)), thickness);
			context.DrawLine(pen, center + new Vector(dx, dy) * ringInner, center + new Vector(dx, dy) * radius);
		}

		var cross = new Pen(new SolidColorBrush(Color.FromArgb(50, 255, 255, 255)));
		context.DrawLine(cross, center - new Vector(ringInner, 0), center + new Vector(ringInner, 0));
		context.DrawLine(cross, center - new Vector(0, ringInner), center + new Vector(0, ringInner));

		var (cb, cr) = ChromaFromTint(R, G, B);
		var dot = center + new Vector(cb, -cr) * (ringInner / RimChroma);
		context.DrawLine(new Pen(new SolidColorBrush(Color.FromArgb(120, 255, 255, 255))), center, dot);
		context.DrawEllipse(
			new SolidColorBrush(DisplayColor(cb / RimChroma, cr / RimChroma, 1.0)), new Pen(Brushes.White, 1.5), dot, 5, 5);
	}

	/// Exaggerated so a small tint still reads on screen
	private static Color DisplayColor(double cb, double cr, double strength) {
		var (r, g, b) = TintFromChroma(cb * RimChroma * strength, cr * RimChroma * strength);
		static byte Channel(float v) {
			return (byte)Math.Clamp((v - 0.35) * 255 / 1.3, 0, 255);
		}

		return Color.FromRgb(Channel(r), Channel(g), Channel(b));
	}

	protected override void OnPointerPressed(PointerPressedEventArgs e) {
		base.OnPointerPressed(e);
		if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		if (e.ClickCount == 2) {
			Reset();
			e.Handled = true;
			return;
		}

		m_dragging = true;
		m_lastPointer = e.GetPosition(this);
		e.Pointer.Capture(this);
		e.Handled = true;
	}

	protected override void OnPointerMoved(PointerEventArgs e) {
		base.OnPointerMoved(e);
		if (!m_dragging) return;
		var position = e.GetPosition(this);
		var delta = position - m_lastPointer;
		m_lastPointer = position;

		var radius = Math.Min(Bounds.Width, Bounds.Height) / 2 * 0.9;
		if (radius <= 0) return;
		var scale = RimChroma / radius * DragFactor * (e.KeyModifiers.HasFlag(KeyModifiers.Shift) ? FineFactor : 1.0);
		var (cb, cr) = ChromaFromTint(R, G, B);
		cb += delta.X * scale;
		cr -= delta.Y * scale;

		var length = Math.Sqrt(cb * cb + cr * cr);
		if (length > RimChroma) {
			cb *= RimChroma / length;
			cr *= RimChroma / length;
		}

		(R, G, B) = TintFromChroma(cb, cr);
		e.Handled = true;
	}

	protected override void OnPointerReleased(PointerReleasedEventArgs e) {
		base.OnPointerReleased(e);
		if (!m_dragging) return;
		m_dragging = false;
		e.Pointer.Capture(null);
	}

	protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e) {
		base.OnPointerCaptureLost(e);
		m_dragging = false;
	}

	public void Reset() {
		(R, G, B) = (1f, 1f, 1f);
	}
}
