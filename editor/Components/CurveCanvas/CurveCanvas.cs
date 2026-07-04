//
// CurveCanvas.cs by Xein
// 04 Jul 2026
//

using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Media;
using editor.Assets;

namespace editor.Components.CurveCanvas;

public sealed class CurveCanvas : Control {
	private const double HitRadius = 9;
	private const double SampleSteps = 160;
	private const double InsetLeft = 44, InsetBottom = 22, InsetTop = 10, InsetRight = 10;
	private const double GridTargetPx = 80;
	private const string Font = "FiraCode Nerd Font,Consolas,monospace";

	public static readonly DirectProperty<CurveCanvas, IList<CurveCanvasItem>?> CurvesProperty =
		AvaloniaProperty.RegisterDirect<CurveCanvas, IList<CurveCanvasItem>?>(
			nameof(Curves), o => o.Curves, (o, v) => o.Curves = v);

	public static readonly StyledProperty<int> ActiveCurveIndexProperty =
		AvaloniaProperty.Register<CurveCanvas, int>(nameof(ActiveCurveIndex));

	public static readonly StyledProperty<int> SelectedPointIndexProperty =
		AvaloniaProperty.Register<CurveCanvas, int>(nameof(SelectedPointIndex), -1,
			defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<double> XMinProperty =
		AvaloniaProperty.Register<CurveCanvas, double>(nameof(XMin), double.NaN);

	public static readonly StyledProperty<double> XMaxProperty =
		AvaloniaProperty.Register<CurveCanvas, double>(nameof(XMax), double.NaN);

	public static readonly StyledProperty<double> YMinProperty =
		AvaloniaProperty.Register<CurveCanvas, double>(nameof(YMin), double.NaN);

	public static readonly StyledProperty<double> YMaxProperty =
		AvaloniaProperty.Register<CurveCanvas, double>(nameof(YMax), double.NaN);

	public static readonly StyledProperty<bool> IsReadOnlyProperty =
		AvaloniaProperty.Register<CurveCanvas, bool>(nameof(IsReadOnly));

	static CurveCanvas() {
		AffectsRender<CurveCanvas>(ActiveCurveIndexProperty, SelectedPointIndexProperty,
			XMinProperty, XMaxProperty, YMinProperty, YMaxProperty, IsReadOnlyProperty);
		FocusableProperty.OverrideDefaultValue<CurveCanvas>(true);
	}

	private IList<CurveCanvasItem>? m_curves;
	private readonly List<CurveCanvasItem> m_hooked = [];

	private int m_hoverIndex = -1;
	private int m_dragIndex = -1;
	// Data window is frozen while dragging so the fit doesn't chase the point under the cursor
	private bool m_freezeView;
	private (double X0, double X1, double Y0, double Y1) m_view = (0, 1, 0, 1);

	public IList<CurveCanvasItem>? Curves {
		get => m_curves;
		set {
			var old = m_curves;
			if (!SetAndRaise(CurvesProperty, ref m_curves, value)) return;
			if (old is INotifyCollectionChanged oldNcc) oldNcc.CollectionChanged -= OnCollectionChanged;
			if (value is INotifyCollectionChanged ncc) ncc.CollectionChanged += OnCollectionChanged;
			RehookItems();
			InvalidateVisual();
		}
	}

	public int ActiveCurveIndex {
		get => GetValue(ActiveCurveIndexProperty);
		set => SetValue(ActiveCurveIndexProperty, value);
	}

	public int SelectedPointIndex {
		get => GetValue(SelectedPointIndexProperty);
		set => SetValue(SelectedPointIndexProperty, value);
	}

	public double XMin { get => GetValue(XMinProperty); set => SetValue(XMinProperty, value); }
	public double XMax { get => GetValue(XMaxProperty); set => SetValue(XMaxProperty, value); }
	public double YMin { get => GetValue(YMinProperty); set => SetValue(YMinProperty, value); }
	public double YMax { get => GetValue(YMaxProperty); set => SetValue(YMaxProperty, value); }

	public bool IsReadOnly {
		get => GetValue(IsReadOnlyProperty);
		set => SetValue(IsReadOnlyProperty, value);
	}

	private CurveCanvasItem? ActiveItem =>
		m_curves is { } c && ActiveCurveIndex >= 0 && ActiveCurveIndex < c.Count
			? c[ActiveCurveIndex]
			: null;

	private bool CanEdit => !IsReadOnly && ActiveItem is { IsEditable: true, IsVisible: true };

	private void OnCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e) {
		RehookItems();
		InvalidateVisual();
	}

	private void RehookItems() {
		foreach (var item in m_hooked) {
			item.Changed -= OnItemChanged;
			item.PropertyChanged -= OnItemPropertyChanged;
		}
		m_hooked.Clear();
		if (m_curves is null) return;
		foreach (var item in m_curves) {
			item.Changed += OnItemChanged;
			item.PropertyChanged += OnItemPropertyChanged;
			m_hooked.Add(item);
		}
	}

	private void OnItemChanged() => InvalidateVisual();

	private void OnItemPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e) =>
		InvalidateVisual();

	private Rect PlotRect => new(
		InsetLeft, InsetTop,
		Math.Max(1, Bounds.Width - InsetLeft - InsetRight),
		Math.Max(1, Bounds.Height - InsetTop - InsetBottom));

	private void UpdateView() {
		if (m_freezeView) return;

		double x0 = double.PositiveInfinity, x1 = double.NegativeInfinity;
		double y0 = double.PositiveInfinity, y1 = double.NegativeInfinity;

		if (m_curves is { } curves) {
			foreach (var item in curves) {
				if (!item.IsVisible) continue;
				for (int i = 0; i < item.NumPoints; i++) {
					var (px, py) = item.GetPoint(i);
					x0 = Math.Min(x0, px); x1 = Math.Max(x1, px);
					y0 = Math.Min(y0, py); y1 = Math.Max(y1, py);
				}
			}
		}

		if (double.IsInfinity(x0)) { x0 = 0; x1 = 1; y0 = 0; y1 = 1; }

		// pinned axes win over the data bounds
		if (!double.IsNaN(XMin)) x0 = XMin;
		if (!double.IsNaN(XMax)) x1 = XMax;
		if (!double.IsNaN(YMin)) y0 = YMin;
		if (!double.IsNaN(YMax)) y1 = YMax;

		if (x1 - x0 < 1e-6) { x0 -= 0.5; x1 += 0.5; }
		if (y1 - y0 < 1e-6) { y0 -= 0.5; y1 += 0.5; }

		// breathing room on auto-fit axes only
		double padX = (x1 - x0) * 0.08, padY = (y1 - y0) * 0.08;
		if (double.IsNaN(XMin)) x0 -= padX;
		if (double.IsNaN(XMax)) x1 += padX;
		if (double.IsNaN(YMin)) y0 -= padY;
		if (double.IsNaN(YMax)) y1 += padY;

		m_view = (x0, x1, y0, y1);
	}

	private Point ToPixel(double x, double y) {
		var plot = PlotRect;
		return new Point(
			plot.X + (x - m_view.X0) / (m_view.X1 - m_view.X0) * plot.Width,
			plot.Bottom - (y - m_view.Y0) / (m_view.Y1 - m_view.Y0) * plot.Height);
	}

	private (float X, float Y) ToData(Point p) {
		var plot = PlotRect;
		return (
			(float)(m_view.X0 + (p.X - plot.X) / plot.Width * (m_view.X1 - m_view.X0)),
			(float)(m_view.Y0 + (plot.Bottom - p.Y) / plot.Height * (m_view.Y1 - m_view.Y0)));
	}

	public override void Render(DrawingContext ctx) {
		UpdateView();

		var bg1 = Resolve("Bg1", "#121111");
		var bg4 = Resolve("Bg4", "#383637");
		var bg5 = Resolve("Bg5", "#454343");
		var muted = Resolve("TextMuted", "#a0a0a0");
		var red = Resolve("Red", "#ff1659");

		var bounds = new Rect(Bounds.Size);
		ctx.DrawRectangle(bg1, new Pen(bg5), new RoundedRect(bounds.Deflate(0.5), 6));

		using var clip = ctx.PushClip(new RoundedRect(bounds, 6));

		DrawGrid(ctx, bg4, bg5, muted);

		if (m_curves is not { } curves) return;

		// read-only canvases are previews: everything full color, no editing affordances
		var active = IsReadOnly ? null : ActiveItem;
		foreach (var item in curves) {
			if (!item.IsVisible || item == active) continue;
			DrawCurve(ctx, item, dimmed: !IsReadOnly);
			DrawPoints(ctx, item, bg1, red, active: IsReadOnly);
		}
		if (active is { IsVisible: true }) {
			DrawCurve(ctx, active, dimmed: false);
			DrawControlPolygon(ctx, active);
			DrawPoints(ctx, active, bg1, red, active: true);
		}
	}

	private void DrawGrid(DrawingContext ctx, IBrush zeroBrush, IBrush lineBrush, IBrush textBrush) {
		var plot = PlotRect;
		var typeface = new Typeface(Font);
		var linePen = new Pen(lineBrush, 1);
		var zeroPen = new Pen(zeroBrush, 1);

		double stepX = NiceStep((m_view.X1 - m_view.X0) / Math.Max(1, plot.Width / GridTargetPx));
		double stepY = NiceStep((m_view.Y1 - m_view.Y0) / Math.Max(1, plot.Height / GridTargetPx));

		for (double x = Math.Ceiling(m_view.X0 / stepX) * stepX; x <= m_view.X1; x += stepX) {
			double px = ToPixel(x, 0).X;
			bool zero = Math.Abs(x) < stepX * 1e-6;
			ctx.DrawLine(zero ? zeroPen : linePen, new Point(px, plot.Y), new Point(px, plot.Bottom));

			var label = Format(x, typeface, textBrush);
			ctx.DrawText(label, new Point(px - label.Width / 2, plot.Bottom + 4));
		}

		for (double y = Math.Ceiling(m_view.Y0 / stepY) * stepY; y <= m_view.Y1; y += stepY) {
			double py = ToPixel(0, y).Y;
			bool zero = Math.Abs(y) < stepY * 1e-6;
			ctx.DrawLine(zero ? zeroPen : linePen, new Point(plot.X, py), new Point(plot.Right, py));

			var label = Format(y, typeface, textBrush);
			ctx.DrawText(label, new Point(plot.X - label.Width - 6, py - label.Height / 2));
		}
	}

	private void DrawCurve(DrawingContext ctx, CurveCanvasItem item, bool dimmed) {
		if (item.NumPoints < 2) return;

		var geometry = new StreamGeometry();
		using (var g = geometry.Open()) {
			float tScale = item.Curve.TScale;
			bool is3D = item.Curve.Dimension == CurveDimension.D3;
			for (int i = 0; i <= SampleSteps; i++) {
				float t = (float)(i / SampleSteps) * tScale;
				float x, y;
				if (is3D) (x, y, _) = item.Curve.Eval3D(t);
				else (x, y) = item.Curve.Eval2D(t);

				var p = ToPixel(x, y);
				if (i == 0) g.BeginFigure(p, false);
				else g.LineTo(p);
			}
			g.EndFigure(false);
		}

		var color = dimmed ? Fade(item.Color, 0.4) : item.Color;
		ctx.DrawGeometry(null, new Pen(new SolidColorBrush(color), 2) {
			LineCap = PenLineCap.Round, LineJoin = PenLineJoin.Round
		}, geometry);
	}

	private void DrawControlPolygon(DrawingContext ctx, CurveCanvasItem item) {
		if (item.NumPoints < 2) return;

		// Bezier: slope stems from each anchor to its tangent handles
		if (item.Curve.SplineType == SplineType.Bezier && (item.NumPoints - 1) % 3 == 0) {
			var stemPen = new Pen(new SolidColorBrush(Fade(item.Color, 0.55)), 1);
			for (int i = 0; i < item.NumPoints; i++) {
				if (!item.IsHandle(i)) continue;
				int anchor = i % 3 == 1 ? i - 1 : i + 1;
				ctx.DrawLine(stemPen,
					ToPixel(item.GetPoint(anchor).X, item.GetPoint(anchor).Y),
					ToPixel(item.GetPoint(i).X, item.GetPoint(i).Y));
			}
			return;
		}

		// B-Spline: faint control polygon
		if (item.Curve.SplineType != SplineType.BSpline) return;
		var pen = new Pen(new SolidColorBrush(Fade(item.Color, 0.35)), 1);
		var prev = ToPixel(item.GetPoint(0).X, item.GetPoint(0).Y);
		for (int i = 1; i < item.NumPoints; i++) {
			var p = ToPixel(item.GetPoint(i).X, item.GetPoint(i).Y);
			ctx.DrawLine(pen, prev, p);
			prev = p;
		}
	}

	private void DrawPoints(DrawingContext ctx, CurveCanvasItem item, IBrush fill, IBrush selectedFill, bool active) {
		var stroke = new SolidColorBrush(active ? item.Color : Fade(item.Color, 0.4));
		var pen = new Pen(stroke, 2);

		for (int i = 0; i < item.NumPoints; i++) {
			var p = ToPixel(item.GetPoint(i).X, item.GetPoint(i).Y);
			bool selected = active && i == SelectedPointIndex;
			bool hovered = active && i == m_hoverIndex;
			double r = selected ? 6 : hovered ? 5.5 : 4.5;

			if (item.IsHandle(i)) {
				// tangent handles: small hollow squares
				double h = r - 1;
				ctx.DrawRectangle(fill, pen, new Rect(p.X - h, p.Y - h, h * 2, h * 2));
			} else {
				ctx.DrawEllipse(selected ? selectedFill : fill, pen, p, r, r);
			}
		}
	}

	private static FormattedText Format(double value, Typeface typeface, IBrush brush) =>
		new(value.ToString("0.###", CultureInfo.InvariantCulture),
			CultureInfo.InvariantCulture, FlowDirection.LeftToRight, typeface, 11, brush);

	// grid steps snap to 1/2/5 × 10^n
	private static double NiceStep(double raw) {
		if (raw <= 0 || double.IsNaN(raw) || double.IsInfinity(raw)) return 1;
		double mag = Math.Pow(10, Math.Floor(Math.Log10(raw)));
		double norm = raw / mag;
		return (norm < 1.5 ? 1 : norm < 3.5 ? 2 : norm < 7.5 ? 5 : 10) * mag;
	}

	private static Color Fade(Color c, double opacity) =>
		Color.FromArgb((byte)(c.A * opacity), c.R, c.G, c.B);

	// For view models building CurveCanvasItems out of the theme accents
	public static Color ResolveColor(string key, string fallback) {
		if (Application.Current is { } app &&
		    app.TryGetResource(key, app.ActualThemeVariant, out var value) &&
		    value is ISolidColorBrush brush)
			return brush.Color;
		return Color.Parse(fallback);
	}

	private static IBrush Resolve(string key, string fallback) {
		if (Application.Current is { } app &&
		    app.TryGetResource(key, app.ActualThemeVariant, out var value) && value is IBrush brush)
			return brush;
		return new SolidColorBrush(Color.Parse(fallback));
	}

	protected override void OnPointerPressed(PointerPressedEventArgs e) {
		base.OnPointerPressed(e);
		Focus();
		if (!CanEdit || ActiveItem is not { } item) return;

		var pos = e.GetPosition(this);
		var props = e.GetCurrentPoint(this).Properties;
		int hit = HitTest(item, pos);

		if (props.IsRightButtonPressed) {
			if (hit >= 0) RemoveAt(item, hit);
			e.Handled = true;
			return;
		}

		if (!props.IsLeftButtonPressed) return;

		if (e.ClickCount == 2 && hit < 0) {
			var (x, y) = ClampToPins(ToData(pos));
			SelectedPointIndex = item.InsertPoint(x, y);
			e.Handled = true;
			return;
		}

		if (hit >= 0) {
			SelectedPointIndex = hit;
			m_dragIndex = hit;
			m_freezeView = true;
			e.Pointer.Capture(this);
		} else {
			SelectedPointIndex = -1;
		}
		e.Handled = true;
	}

	protected override void OnPointerMoved(PointerEventArgs e) {
		base.OnPointerMoved(e);
		if (ActiveItem is not { } item) return;

		var pos = e.GetPosition(this);

		if (m_dragIndex >= 0) {
			var (x, y) = ClampToPins(ToData(pos));
			item.MovePoint(m_dragIndex, x, y);
			e.Handled = true;
			return;
		}

		int hover = CanEdit ? HitTest(item, pos) : -1;
		if (hover != m_hoverIndex) {
			m_hoverIndex = hover;
			Cursor = hover >= 0 ? new Cursor(StandardCursorType.Hand) : Cursor.Default;
			InvalidateVisual();
		}
	}

	protected override void OnPointerReleased(PointerReleasedEventArgs e) {
		base.OnPointerReleased(e);
		if (m_dragIndex < 0) return;
		m_dragIndex = -1;
		m_freezeView = false;
		e.Pointer.Capture(null);
		InvalidateVisual(); // re-fit after the drag
	}

	protected override void OnPointerExited(PointerEventArgs e) {
		base.OnPointerExited(e);
		if (m_hoverIndex < 0) return;
		m_hoverIndex = -1;
		Cursor = Cursor.Default;
		InvalidateVisual();
	}

	protected override void OnKeyDown(KeyEventArgs e) {
		base.OnKeyDown(e);
		if (e.Key is not (Key.Delete or Key.Back)) return;
		if (!CanEdit || ActiveItem is not { } item || SelectedPointIndex < 0) return;
		RemoveAt(item, SelectedPointIndex);
		e.Handled = true;
	}

	private void RemoveAt(CurveCanvasItem item, int index) {
		if (!item.RemovePoint(index)) return;
		m_hoverIndex = -1;
		SelectedPointIndex = Math.Min(SelectedPointIndex, item.NumPoints - 1);
	}

	private int HitTest(CurveCanvasItem item, Point pos) {
		int best = -1;
		double bestDist = HitRadius;
		for (int i = 0; i < item.NumPoints; i++) {
			var p = ToPixel(item.GetPoint(i).X, item.GetPoint(i).Y);
			double dist = Math.Sqrt((p.X - pos.X) * (p.X - pos.X) + (p.Y - pos.Y) * (p.Y - pos.Y));
			if (dist < bestDist) { bestDist = dist; best = i; }
		}
		return best;
	}

	private (float X, float Y) ClampToPins((float X, float Y) d) {
		float x = d.X, y = d.Y;
		if (!double.IsNaN(XMin)) x = Math.Max(x, (float)XMin);
		if (!double.IsNaN(XMax)) x = Math.Min(x, (float)XMax);
		if (!double.IsNaN(YMin)) y = Math.Max(y, (float)YMin);
		if (!double.IsNaN(YMax)) y = Math.Min(y, (float)YMax);
		return (x, y);
	}
}
