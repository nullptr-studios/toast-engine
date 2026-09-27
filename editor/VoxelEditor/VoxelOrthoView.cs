using System;
using System.ComponentModel;
using System.Globalization;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using System.Runtime.InteropServices;

namespace editor.VoxelEditor;

public enum OrthoProjection { Front, Side, Top }

public sealed class VoxelOrthoView : Control {
	public static readonly StyledProperty<OrthoProjection> ProjectionProperty =
		AvaloniaProperty.Register<VoxelOrthoView, OrthoProjection>(nameof(Projection));

	private const double HandleSize = 8;
	private const double MinScale = 1;
	private const double MaxScale = 80;

	private static readonly Typeface s_labelFont = new("Consolas");

	private VoxelEditorViewModel? m_editor;
	private double m_scale = 6;    // pixels per voxel
	private Point m_origin;        // where voxel 0,0 is on screen
	private bool m_fitted;

	// Pointer state
	private Drag m_drag;
	private Point m_pressed;
	private Point m_last;
	private VoxelPieceInfo? m_dragPiece;
	private (int H0, int V0, int H1, int V1) m_dragBox;
	private (Point A, Point B)? m_cutLine;
	private bool m_awaitingSliceSide;
	private bool m_boxClickMode;    // a plain click started the box, it follows the mouse until the next click

	private enum Drag { None, Pan, Move, EdgeMinH, EdgeMaxH, EdgeMinV, EdgeMaxV, Extrude, Box, Cut }

	static VoxelOrthoView() {
		AffectsRender<VoxelOrthoView>(ProjectionProperty);
		FocusableProperty.OverrideDefaultValue<VoxelOrthoView>(true);
	}

	public VoxelOrthoView() {
		PointerTouchPadGestureMagnify += OnTouchpadMagnify;
	}

	public OrthoProjection Projection {
		get => GetValue(ProjectionProperty);
		set => SetValue(ProjectionProperty, value);
	}

	public VoxelEditorViewModel? Editor {
		get => m_editor;
		set {
			if (m_editor is not null) {
				m_editor.LayoutChanged -= OnLayoutChanged;
				m_editor.SelectionChanged -= InvalidateVisual;
				m_editor.PropertyChanged -= OnEditorChanged;
			}
			m_editor = value;
			m_fitted = false;
			if (m_editor is not null) {
				m_editor.LayoutChanged += OnLayoutChanged;
				m_editor.SelectionChanged += InvalidateVisual;
				m_editor.PropertyChanged += OnEditorChanged;
			}
			InvalidateVisual();
		}
	}

	// Shape axis drawn left to right, bottom to top and the one looked along
	public int HAxis => Projection == OrthoProjection.Side ? 1 : 0;
	public int VAxis => Projection == OrthoProjection.Top ? 1 : 2;
	public int DepthAxis => Projection switch { OrthoProjection.Front => 1, OrthoProjection.Side => 0, _ => 2 };

	private void OnLayoutChanged() {
		RebuildProjection();
		if (!m_fitted) Fit();
		InvalidateVisual();
	}

	private void OnEditorChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName is nameof(VoxelEditorViewModel.ActiveTool) or nameof(VoxelEditorViewModel.ShowVoxelGrid)
		    or nameof(VoxelEditorViewModel.ShowUnitGrid) or nameof(VoxelEditorViewModel.FourUp)) {
			CancelCut();
			if (e.PropertyName == nameof(VoxelEditorViewModel.FourUp) && !m_fitted) Fit();
			InvalidateVisual();
		}
	}

	// Frames every piece with a margin
	private void Fit() {
		if (m_editor is not { Pieces.Count: > 0 } editor || Bounds.Width < 10 || Bounds.Height < 10) return;
		var h0 = editor.Pieces.Min(p => p.Axis(HAxis, false));
		var h1 = editor.Pieces.Max(p => p.Axis(HAxis, true)) + 1;
		var v0 = editor.Pieces.Min(p => p.Axis(VAxis, false));
		var v1 = editor.Pieces.Max(p => p.Axis(VAxis, true)) + 1;
		var w = Math.Max(h1 - h0, 10);
		var h = Math.Max(v1 - v0, 10);
		m_scale = Math.Clamp(Math.Min(Bounds.Width / (w * 1.3), Bounds.Height / (h * 1.3)), MinScale, MaxScale);
		var centre = new Point((h0 + h1) * 0.5, (v0 + v1) * 0.5);
		m_origin = new Point(Bounds.Width * 0.5 - centre.X * m_scale, Bounds.Height * 0.5 + centre.Y * m_scale);
		m_fitted = true;
	}

	private Point ToScreen(double h, double v) {
		return new Point(m_origin.X + h * m_scale, m_origin.Y - v * m_scale);
	}

	private (double H, double V) ToVoxel(Point p) {
		return ((p.X - m_origin.X) / m_scale, (m_origin.Y - p.Y) / m_scale);
	}

	private Rect RectOf(int h0, int v0, int h1, int v1) {
		return new Rect(ToScreen(h0, v1 + 1), ToScreen(h1 + 1, v0));
	}

	private Rect RectOf(VoxelPieceInfo piece) {
		return RectOf(piece.Axis(HAxis, false), piece.Axis(VAxis, false), piece.Axis(HAxis, true), piece.Axis(VAxis, true));
	}

	private (int H0, int V0, int H1, int V1) BoxOf(VoxelPieceInfo piece) {
		return (piece.Axis(HAxis, false), piece.Axis(VAxis, false), piece.Axis(HAxis, true), piece.Axis(VAxis, true));
	}

	private WriteableBitmap? m_projectionImage;
	private readonly System.Collections.Generic.List<(Color Colour, StreamGeometry Lines)> m_contours = [];
	private VoxelProjectionInfo? m_projection;

	private int ProjectionIndex => Projection switch { OrthoProjection.Front => 0, OrthoProjection.Side => 1, _ => 2 };

	private void RebuildProjection() {
		m_projectionImage?.Dispose();
		m_projectionImage = null;
		m_contours.Clear();
		m_projection = m_editor is { Projections.Count: 3 } editor ? editor.Projections[ProjectionIndex] : null;
		if (m_projection is not { Width: > 0, Height: > 0 } projection) return;

		// Rows run up in the engine and down on screen
		var pixels = new byte[projection.Width * projection.Height * 4];
		for (var v = 0; v < projection.Height; ++v)
		for (var h = 0; h < projection.Width; ++h) {
			var src = (h + v * projection.Width) * 4;
			var dst = (h + (projection.Height - 1 - v) * projection.Width) * 4;
			pixels[dst + 0] = projection.Colors[src + 2];
			pixels[dst + 1] = projection.Colors[src + 1];
			pixels[dst + 2] = projection.Colors[src + 0];
			pixels[dst + 3] = projection.Colors[src + 3];
		}
		m_projectionImage = new WriteableBitmap(new PixelSize(projection.Width, projection.Height), new Vector(96, 96),
			PixelFormat.Bgra8888, AlphaFormat.Unpremul);
		using (var frame = m_projectionImage.Lock()) {
			for (var row = 0; row < projection.Height; ++row)
				Marshal.Copy(pixels, row * projection.Width * 4, frame.Address + row * frame.RowBytes, projection.Width * 4);
		}

		// oUtlines so shapes behind others still show through
		var contexts = new System.Collections.Generic.Dictionary<Color, StreamGeometryContext>();
		void Segment(byte[] edges, int index, double h0, double v0, double h1, double v1) {
			if (edges.Length < (index + 1) * 4 || edges[index * 4 + 3] == 0) return;
			var colour = Color.FromRgb(edges[index * 4], edges[index * 4 + 1], edges[index * 4 + 2]);
			if (!contexts.TryGetValue(colour, out var g)) {
				var geometry = new StreamGeometry();
				g = geometry.Open();
				contexts[colour] = g;
				m_contours.Add((colour, geometry));
			}
			g.BeginFigure(new Point(h0 + projection.MinH, v0 + projection.MinV), false);
			g.LineTo(new Point(h1 + projection.MinH, v1 + projection.MinV));
			g.EndFigure(false);
		}
		for (var v = 0; v < projection.Height; ++v)
		for (var h = 0; h <= projection.Width; ++h)
			Segment(projection.EdgesH, h + v * (projection.Width + 1), h, v, h, v + 1);
		for (var v = 0; v <= projection.Height; ++v)
		for (var h = 0; h < projection.Width; ++h)
			Segment(projection.EdgesV, h + v * projection.Width, h, v, h + 1, v);
		foreach (var context in contexts.Values) context.Dispose();
	}

	private void DrawProjection(DrawingContext context) {
		if (m_projection is not { } projection || m_projectionImage is null) return;
		var dest = RectOf(projection.MinH, projection.MinV, projection.MinH + projection.Width - 1, projection.MinV + projection.Height - 1);
		// Lines first so the fill of whatever is in front tints the ones behind it
		using (context.PushTransform(new Matrix(m_scale, 0, 0, -m_scale, m_origin.X, m_origin.Y))) {
			foreach (var (colour, lines) in m_contours)
				context.DrawGeometry(null, new Pen(new SolidColorBrush(colour), 1.5 / m_scale), lines);
		}
		using (context.PushRenderOptions(new RenderOptions { BitmapInterpolationMode = BitmapInterpolationMode.None })) {
			context.DrawImage(m_projectionImage, new Rect(m_projectionImage.Size), dest);
		}
	}

	public override void Render(DrawingContext context) {
		var bounds = new Rect(Bounds.Size);
		context.FillRectangle(Brush("Bg1", Brushes.Black), bounds);
		using var clip = context.PushClip(bounds);

		DrawGrid(context, bounds);
		if (m_editor is null) return;

		DrawProjection(context);

		// Volume boxes only when h volume edge on
		foreach (var piece in m_editor.Pieces) {
			var selected = piece.Uid == m_editor.SelectedUid;
			if (!selected && !m_editor.ShowEdges) continue;
			var dragging = m_dragPiece?.Uid == piece.Uid && m_drag is not (Drag.None or Drag.Extrude);
			var box = dragging ? m_dragBox : BoxOf(piece);
			DrawPiece(context, piece, box, selected);
		}

		if (m_drag is Drag.Box or Drag.Extrude) DrawPreviewBox(context);
		if (m_cutLine is { } line) DrawCut(context, bounds, line.A, line.B);
		DrawLabel(context, Projection.ToString(), new Point(8, 6), Brush("TextMuted", Brushes.Gray));
	}

	private void DrawGrid(DrawingContext context, Rect bounds) {
		var (h0, v1) = ToVoxel(bounds.TopLeft);
		var (h1, v0) = ToVoxel(bounds.BottomRight);
		var fine = new Pen(new SolidColorBrush(Color.FromArgb(22, 255, 255, 255)));
		var metre = new Pen(new SolidColorBrush(Color.FromArgb(55, 255, 255, 255)));
		var axis = new Pen(new SolidColorBrush(Color.FromArgb(110, 255, 255, 255)));
		var voxelGrid = m_scale >= 6;
		var unitGrid = true;

		for (var h = (int)Math.Floor(h0); h <= (int)Math.Ceiling(h1); ++h) {
			var pen = h == 0 ? axis : h % 10 == 0 ? unitGrid ? metre : null : voxelGrid ? fine : null;
			if (pen is null) continue;
			var x = ToScreen(h, 0).X;
			context.DrawLine(pen, new Point(x, 0), new Point(x, bounds.Height));
		}

		for (var v = (int)Math.Floor(v0); v <= (int)Math.Ceiling(v1); ++v) {
			var pen = v == 0 ? axis : v % 10 == 0 ? unitGrid ? metre : null : voxelGrid ? fine : null;
			if (pen is null) continue;
			var y = ToScreen(0, v).Y;
			context.DrawLine(pen, new Point(0, y), new Point(bounds.Width, y));
		}
	}

	private void DrawPiece(DrawingContext context, VoxelPieceInfo piece, (int H0, int V0, int H1, int V1) box, bool selected) {
		var rect = RectOf(box.H0, box.V0, box.H1, box.V1);
		var colour = piece.Color;
		var dash = piece.Kind switch { 1 => new DashStyle([4, 3], 0), 2 => new DashStyle([1, 3], 0), _ => null };
		var pen = new Pen(new SolidColorBrush(colour), selected ? 2.5 : 1.25, dash);
		// Volume bounds dont get filled
		context.DrawRectangle(pen, rect);

		foreach (var plane in piece.Planes) DrawPlane(context, rect, piece, plane, colour);

		if (!selected) return;

		// measurements text
		var width = (box.H1 - box.H0 + 1) * VoxelEditorViewModel.VoxelSize;
		var height = (box.V1 - box.V0 + 1) * VoxelEditorViewModel.VoxelSize;
		var text = Brush("Text", Brushes.White);
		DrawLabel(context, $"{width.ToString("0.0", CultureInfo.InvariantCulture)} m",
			new Point(rect.Center.X - 16, rect.Top - 18), text);
		DrawLabel(context, $"{height.ToString("0.0", CultureInfo.InvariantCulture)} m",
			new Point(rect.Right + 6, rect.Center.Y - 7), text);

		if (m_editor?.ActiveTool is VoxelTool.Select or VoxelTool.Move or VoxelTool.Extrude) {
			var handle = new SolidColorBrush(colour);
			var outline = new Pen(Brushes.Black);
			if (piece.Resizable || m_editor.ActiveTool == VoxelTool.Extrude)
				foreach (var p in EdgeHandles(rect))
					context.DrawRectangle(handle, outline, HandleRect(p));
			context.DrawEllipse(handle, outline, rect.Center, HandleSize * 0.6, HandleSize * 0.6);
		}
	}

	// A clip plane seen from this side
	private void DrawPlane(
		DrawingContext context, Rect rect, VoxelPieceInfo piece, (double X, double Y, double Z, double W) plane, Color colour) {
		double[] n = [plane.X, plane.Y, plane.Z];
		var depth = (piece.Axis(DepthAxis, false) + piece.Axis(DepthAxis, true) + 1) * 0.5;
		var nh = n[HAxis];
		var nv = n[VAxis];
		var c = n[DepthAxis] * depth + plane.W;
		if (Math.Abs(nh) < 1e-6 && Math.Abs(nv) < 1e-6) return;

		// nh * h + nv * v + c = 0 across the piece
		var (h0, v1) = ToVoxel(rect.TopLeft);
		var (h1, v0) = ToVoxel(rect.BottomRight);
		Point a;
		Point b;
		if (Math.Abs(nv) > Math.Abs(nh)) {
			a = ToScreen(h0, -(nh * h0 + c) / nv);
			b = ToScreen(h1, -(nh * h1 + c) / nv);
		} else {
			a = ToScreen(-(nv * v0 + c) / nh, v0);
			b = ToScreen(-(nv * v1 + c) / nh, v1);
		}
		using var clip = context.PushClip(rect);
		context.DrawLine(new Pen(new SolidColorBrush(colour), 1.5, new DashStyle([6, 2, 1, 2], 0)), a, b);
	}

	private void DrawPreviewBox(DrawingContext context) {
		var rect = RectOf(m_dragBox.H0, m_dragBox.V0, m_dragBox.H1, m_dragBox.V1);
		var colour = m_editor?.ActiveTool switch {
			VoxelTool.Carve => Colors.OrangeRed,
			VoxelTool.Paint => Colors.DeepSkyBlue,
			_ => Colors.LimeGreen
		};
		context.FillRectangle(new SolidColorBrush(colour, 0.2), rect);
		context.DrawRectangle(new Pen(new SolidColorBrush(colour), 1.5, new DashStyle([4, 2], 0)), rect);
		var width = (m_dragBox.H1 - m_dragBox.H0 + 1) * VoxelEditorViewModel.VoxelSize;
		var height = (m_dragBox.V1 - m_dragBox.V0 + 1) * VoxelEditorViewModel.VoxelSize;
		DrawLabel(context,
			$"{width.ToString("0.0", CultureInfo.InvariantCulture)} x {height.ToString("0.0", CultureInfo.InvariantCulture)} m",
			rect.TopLeft + new Point(0, -18), Brush("Text", Brushes.White));
	}

	// The cut line with the kept sides tinted
	// red is the side the normal points to
	private void DrawCut(DrawingContext context, Rect bounds, Point a, Point b) {
		var dir = b - a;
		var length = Math.Sqrt(dir.X * dir.X + dir.Y * dir.Y);
		if (length < 1) return;
		var unit = new Vector(dir.X / length, dir.Y / length);
		var far = Math.Max(bounds.Width, bounds.Height) * 4;
		var p0 = a - unit * far;
		var p1 = a + unit * far;
		var normal = new Vector(-unit.Y, unit.X);    // screen normal, matches CutPlane

		var red = new StreamGeometry();
		using (var g = red.Open()) {
			g.BeginFigure(p0, true);
			g.LineTo(p1);
			g.LineTo(p1 + normal * far);
			g.LineTo(p0 + normal * far);
			g.EndFigure(true);
		}

		var blue = new StreamGeometry();
		using (var g = blue.Open()) {
			g.BeginFigure(p0, true);
			g.LineTo(p1);
			g.LineTo(p1 - normal * far);
			g.LineTo(p0 - normal * far);
			g.EndFigure(true);
		}

		// TODO: This should use the engine colors
		context.DrawGeometry(new SolidColorBrush(Colors.Red, 0.12), null, red);
		context.DrawGeometry(new SolidColorBrush(Colors.DodgerBlue, 0.12), null, blue);
		context.DrawLine(new Pen(Brushes.White, 1.5), p0, p1);
		if (m_awaitingSliceSide)
			DrawLabel(context, "Click keeps red, Shift+click keeps blue", new Point(8, bounds.Height - 22),
				Brush("Text", Brushes.White));
	}

	private void DrawLabel(DrawingContext context, string text, Point at, IBrush brush) {
		var formatted = new FormattedText(text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, s_labelFont, 12, brush);
		context.DrawText(formatted, at);
	}

	private IBrush Brush(string key, IBrush fallback) {
		return this.TryFindResource(key, out var value) && value is IBrush brush ? brush : fallback;
	}

	private static Point[] EdgeHandles(Rect rect) {
		// min h, max h, min v, max v, v grows up so the bottom edge is min v
		return [
			new Point(rect.Left, rect.Center.Y), new Point(rect.Right, rect.Center.Y),
			new Point(rect.Center.X, rect.Bottom), new Point(rect.Center.X, rect.Top)
		];
	}

	private static Rect HandleRect(Point p) {
		return new Rect(p.X - HandleSize / 2, p.Y - HandleSize / 2, HandleSize, HandleSize);
	}

	protected override void OnSizeChanged(SizeChangedEventArgs e) {
		base.OnSizeChanged(e);
		if (!m_fitted) Fit();
	}

	// Two finger scroll pans, pinch and Ctrl+wheel zoom
	protected override void OnPointerWheelChanged(PointerWheelEventArgs e) {
		base.OnPointerWheelChanged(e);
		if (e.KeyModifiers.HasFlag(KeyModifiers.Control)) {
			ZoomAt(e.GetPosition(this), e.Delta.Y > 0 ? 1.15 : 1 / 1.15);
		} else {
			const double panSpeed = 40.0;
			m_origin += new Vector(e.Delta.X, e.Delta.Y) * panSpeed;
			m_fitted = true;
			InvalidateVisual();
		}
		e.Handled = true;
	}

	private void OnTouchpadMagnify(object? sender, PointerDeltaEventArgs e) {
		var amount = e.Delta.X != 0 ? e.Delta.X : e.Delta.Y;
		ZoomAt(e.GetPosition(this), Math.Max(0.1, 1.0 + amount));
		e.Handled = true;
	}

	private void ZoomAt(Point at, double factor) {
		var (h, v) = ToVoxel(at);
		m_scale = Math.Clamp(m_scale * factor, MinScale, MaxScale);
		m_origin = new Point(at.X - h * m_scale, at.Y + v * m_scale);
		m_fitted = true;
		InvalidateVisual();
	}

	protected override void OnPointerPressed(PointerPressedEventArgs e) {
		base.OnPointerPressed(e);
		Focus();
		if (m_editor is not { } editor) return;
		var point = e.GetCurrentPoint(this);

		// The second click of a clicked box places it
		// the first click stays the anchor
		if (m_boxClickMode && m_drag == Drag.Box) {
			m_boxClickMode = false;
			if (point.Properties.IsLeftButtonPressed) CreateFromBox(editor);
			m_drag = Drag.None;
			InvalidateVisual();
			e.Handled = true;
			return;
		}

		m_pressed = m_last = point.Position;

		// Slice waits for a click on the side to keep
		// lick keeps red and shift click keeps blue
		if (m_awaitingSliceSide && m_cutLine is { } line) {
			if (point.Properties.IsLeftButtonPressed) {
				editor.Cut(CutPlane(line.A, line.B), e.KeyModifiers.HasFlag(KeyModifiers.Shift) ? 2u : 1u);
				CancelCut();
				e.Handled = true;
			}
			return;
		}

		if (point.Properties.IsMiddleButtonPressed || point.Properties.IsRightButtonPressed) {
			m_drag = Drag.Pan;
			e.Pointer.Capture(this);
			e.Handled = true;
			return;
		}
		if (!point.Properties.IsLeftButtonPressed) return;

		e.Pointer.Capture(this);
		e.Handled = true;
		var (h, v) = ToVoxel(point.Position);
		var cell = ((int)Math.Floor(h), (int)Math.Floor(v));

		switch (editor.ActiveTool) {
			case VoxelTool.Buildup or VoxelTool.Carve or VoxelTool.Paint:
				m_drag = Drag.Box;
				m_dragBox = (cell.Item1, cell.Item2, cell.Item1, cell.Item2);
				break;
			case VoxelTool.Split or VoxelTool.Slice:
				m_drag = Drag.Cut;
				m_cutLine = (point.Position, point.Position);
				break;
			case VoxelTool.Bucket:
				BucketAt(editor, cell);
				break;
			default:
				StartSelectOrHandle(editor, point.Position, cell);
				break;
		}

		InvalidateVisual();
	}

	private void StartSelectOrHandle(VoxelEditorViewModel editor, Point at, (int H, int V) cell) {
		if (editor.SelectedPiece is { } selected) {
			var rect = RectOf(selected);
			var handles = EdgeHandles(rect);
			for (var i = 0; i < handles.Length; ++i) {
				if (!HandleRect(handles[i]).Inflate(3).Contains(at)) continue;
				if (editor.ActiveTool == VoxelTool.Extrude) {
					m_drag = Drag.Extrude;
					m_extrudeEdge = i;
				} else if (selected.Resizable) {
					m_drag = (Drag)((int)Drag.EdgeMinH + i);
				} else {
					continue;
				}
				BeginPieceDrag(editor, selected);
				return;
			}

			if (rect.Contains(at) && editor.ActiveTool is VoxelTool.Select or VoxelTool.Move) {
				m_drag = Drag.Move;
				BeginPieceDrag(editor, selected);
				return;
			}
		}

		// Clicking picks the smallest piece under the cursor so inner pieces can be reached
		var hit = editor.Pieces
			.Where(p => RectOf(p).Contains(at))
			.OrderBy(p => RectOf(p).Width * RectOf(p).Height)
			.FirstOrDefault();
		editor.Select(hit?.Uid);
		if (hit is not null && editor.ActiveTool == VoxelTool.Move) {
			m_drag = Drag.Move;
			BeginPieceDrag(editor, hit);
		}
	}

	private int m_extrudeEdge;

	private void BeginPieceDrag(VoxelEditorViewModel editor, VoxelPieceInfo piece) {
		m_dragPiece = piece;
		m_dragBox = BoxOf(piece);
		if (m_drag != Drag.Extrude) editor.BeginEdit(m_drag == Drag.Move ? "Volume moved" : "Volume resized");
	}

	protected override void OnPointerMoved(PointerEventArgs e) {
		base.OnPointerMoved(e);
		var at = e.GetPosition(this);
		var delta = at - m_last;
		m_last = at;
		if (m_editor is not { } editor) return;

		switch (m_drag) {
			case Drag.Pan:
				m_origin += delta;
				m_fitted = true;
				break;
			case Drag.Box: {
				var (h, v) = ToVoxel(at);
				var (h0, v0) = ToVoxel(m_pressed);
				m_dragBox = (Math.Min((int)Math.Floor(h0), (int)Math.Floor(h)), Math.Min((int)Math.Floor(v0), (int)Math.Floor(v)),
					Math.Max((int)Math.Floor(h0), (int)Math.Floor(h)), Math.Max((int)Math.Floor(v0), (int)Math.Floor(v)));
				break;
			}
			case Drag.Cut when m_cutLine is { } line:
				m_cutLine = (line.A, at);
				if (m_editor.ActiveTool is VoxelTool.Split or VoxelTool.Slice && Distance(line.A, at) > 4)
					editor.PreviewCut(CutPlane(line.A, at));
				break;
			case Drag.Move or Drag.EdgeMinH or Drag.EdgeMaxH or Drag.EdgeMinV or Drag.EdgeMaxV or Drag.Extrude
				when m_dragPiece is { } piece:
				UpdatePieceDrag(editor, piece, at);
				break;
			default:
				return;
		}

		InvalidateVisual();
	}

	private void UpdatePieceDrag(VoxelEditorViewModel editor, VoxelPieceInfo piece, Point at) {
		var (h, v) = ToVoxel(at);
		var (ph, pv) = ToVoxel(m_pressed);
		var dh = (int)Math.Round(h - ph);
		var dv = (int)Math.Round(v - pv);
		var (h0, v0, h1, v1) = BoxOf(piece);

		var box = m_drag switch {
			Drag.Move => (h0 + dh, v0 + dv, h1 + dh, v1 + dv),
			Drag.EdgeMinH => (Math.Min(h0 + dh, h1), v0, h1, v1),
			Drag.EdgeMaxH => (h0, v0, Math.Max(h1 + dh, h0), v1),
			Drag.EdgeMinV => (h0, Math.Min(v0 + dv, v1), h1, v1),
			Drag.EdgeMaxV => (h0, v0, h1, Math.Max(v1 + dv, v0)),
			// Extrude grows a new box off the edge it was pulled from
			Drag.Extrude => m_extrudeEdge switch {
				0 => (h0 + Math.Min(dh, -1), v0, h0 - 1, v1),
				1 => (h1 + 1, v0, h1 + Math.Max(dh, 1), v1),
				2 => (h0, v0 + Math.Min(dv, -1), h1, v0 - 1),
				_ => (h0, v1 + 1, h1, v1 + Math.Max(dv, 1))
			},
			_ => m_dragBox
		};
		if (box == m_dragBox) return;
		m_dragBox = box;
		if (m_drag != Drag.Extrude) editor.SetBounds(piece.Uid, ToShape(piece, box, false), ToShape(piece, box, true));
	}

	protected override void OnPointerReleased(PointerReleasedEventArgs e) {
		base.OnPointerReleased(e);
		e.Pointer.Capture(null);
		if (m_editor is not { } editor) {
			m_drag = Drag.None;
			return;
		}

		switch (m_drag) {
			case Drag.Box when m_dragBox.H0 == m_dragBox.H1 && m_dragBox.V0 == m_dragBox.V1 && !m_boxClickMode:
				// click instead of a drag, the box follows the mouse and the next click places it
				// we need this for trackpad people like me
				m_boxClickMode = true;
				InvalidateVisual();
				return;
			case Drag.Box:
				CreateFromBox(editor);
				break;
			case Drag.Extrude when m_dragPiece is { } piece:
				editor.Extrude(piece.Uid, ToShape(piece, m_dragBox, false), ToShape(piece, m_dragBox, true), false);
				break;
			case Drag.Cut when m_cutLine is { } line:
				if (Distance(line.A, line.B) < 4) {
					CancelCut();
				} else if (editor.ActiveTool == VoxelTool.Split) {
					editor.Cut(CutPlane(line.A, line.B), 0);
					CancelCut();
				} else {
					m_awaitingSliceSide = true;
				}
				break;
			case Drag.Move or Drag.EdgeMinH or Drag.EdgeMaxH or Drag.EdgeMinV or Drag.EdgeMaxV:
				editor.CommitEdit();
				break;
		}

		m_drag = Drag.None;
		m_dragPiece = null;
		InvalidateVisual();
	}

	protected override void OnKeyDown(KeyEventArgs e) {
		base.OnKeyDown(e);
		if (e.Key != Key.Escape) return;
		CancelCut();
		m_drag = Drag.None;
		m_editor?.CommitEdit();
		InvalidateVisual();
		e.Handled = true;
	}

	private void CancelCut() {
		m_cutLine = null;
		m_awaitingSliceSide = false;
		m_editor?.PreviewCut(null);
	}

	// A new volume through the depth of the selected piece
	private void CreateFromBox(VoxelEditorViewModel editor) {
		var (d0, d1) = editor.SelectedPiece is { } around
			? (around.Axis(DepthAxis, false), around.Axis(DepthAxis, true))
			: (0, 9);
		int[] min = new int[3];
		int[] max = new int[3];
		min[HAxis] = m_dragBox.H0;
		max[HAxis] = m_dragBox.H1;
		min[VAxis] = m_dragBox.V0;
		max[VAxis] = m_dragBox.V1;
		min[DepthAxis] = d0;
		max[DepthAxis] = d1;
		editor.CreateVolume(editor.ActiveTool, (min[0], min[1], min[2]), (max[0], max[1], max[2]));
	}

	private void BucketAt(VoxelEditorViewModel editor, (int H, int V) cell) {
		int[] voxel = new int[3];
		voxel[HAxis] = cell.H;
		voxel[VAxis] = cell.V;

		// The projection knows the nearest voxel of the cell but i dont trust the eeditor
		// Ask the engine for cconfirmation either way
		if (m_projection is { } projection && projection.Solid(cell.H - projection.MinH, cell.V - projection.MinV)) {
			voxel[DepthAxis] = projection.Depth(cell.H - projection.MinH, cell.V - projection.MinV) * NearStep();
		} else {
			var hit = editor.Pieces
				.Where(p => cell.H >= p.Axis(HAxis, false) && cell.H <= p.Axis(HAxis, true) &&
				            cell.V >= p.Axis(VAxis, false) && cell.V <= p.Axis(VAxis, true))
				.LastOrDefault();
			if (hit is null) return;
			voxel[DepthAxis] = NearSide(hit);
		}
		editor.Bucket((voxel[0], voxel[1], voxel[2]), DepthAxis, NearStep());
	}

	// The side of the depth axis facing the camera and which way is away from it
	private int NearSide(VoxelPieceInfo piece) {
		return NearStep() > 0 ? piece.Axis(DepthAxis, false) : piece.Axis(DepthAxis, true);
	}

	private int NearStep() {
		// if you are reading this i dont know why i wasnt a normal person and did this with fucking
		// vulkan instead of avalonia
		return Projection == OrthoProjection.Front ? 1 : -1;
	}

	private (int X, int Y, int Z) ToShape(VoxelPieceInfo piece, (int H0, int V0, int H1, int V1) box, bool max) {
		int[] v = [piece.Axis(0, max), piece.Axis(1, max), piece.Axis(2, max)];
		v[HAxis] = max ? box.H1 : box.H0;
		v[VAxis] = max ? box.V1 : box.V0;
		return (v[0], v[1], v[2]);
	}

	private (double X, double Y, double Z, double W) CutPlane(Point a, Point b) {
		var (ah, av) = ToVoxel(a);
		var (bh, bv) = ToVoxel(b);
		var dh = bh - ah;
		var dv = bv - av;
		var length = Math.Sqrt(dh * dh + dv * dv);
		if (length < 1e-6) return (0, 0, 0, 0);

		// Screen normal (-dy, dx) in voxel space where v grows up
		var nh = dv / length;
		var nv = -dh / length;
		double[] n = new double[3];
		n[HAxis] = nh;
		n[VAxis] = nv;
		var w = -(nh * ah + nv * av);
		return (n[0], n[1], n[2], w);
	}

	private static double Distance(Point a, Point b) {
		var d = a - b;
		return Math.Sqrt(d.X * d.X + d.Y * d.Y);
	}
}
