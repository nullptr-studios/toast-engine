using System.Collections.Generic;
using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace editor.Workspace;

// You dont even know how much this took -x
public sealed class HierarchyConnectorLayer : Control {
	public const double RowHeight = 26;
	public const double IndentStep = 20;
	public const double LeftPad = 10; // left inset of the whole tree
	public const double SwatchHalf = 8;

	private const double BoxLeftPad = 6;
	private const double BoxRightPad = 8;
	private const double BoxCorner = 8;
	private const double AncestorWidth = 3;
	private const double DescendantWidth = 2;
	private const double DropLineWidth = 2;
	private const double DropDotRadius = 3.5;
	private int m_dropLineDepth;
	private int m_dropLineIndex = -1; // row boundary to draw the insertion line at; -1 = none

	private IReadOnlyList<HierarchyElement> m_rows = [];
	private int m_selectedIndex = -1;
	private int m_hoveredIndex = -1;
	private double[] m_offsets = [0];
	public int HoveredIndex {
		get => m_hoveredIndex;
		set { if (m_hoveredIndex == value) return; m_hoveredIndex = value; InvalidateVisual(); }
	}
	public void RefreshGeometry() {
		m_offsets = new double[m_rows.Count + 1];
		for (var i = 0; i < m_rows.Count; i++) m_offsets[i + 1] = m_offsets[i] + m_rows[i].VisualHeight;
		InvalidateMeasure(); InvalidateVisual();
	}
	public double GetRowTop(int index) => m_offsets[Math.Clamp(index, 0, m_offsets.Length - 1)];
	public int GetRowAt(double y) {
		for (var i = 0; i < m_rows.Count; i++) if (y >= m_offsets[i] && y < m_offsets[i + 1]) return i;
		return -1;
	}

	public IReadOnlyList<HierarchyElement> Rows {
		get => m_rows;
		set {
			m_rows = value;
			RefreshGeometry();
		}
	}

	public int SelectedIndex {
		get => m_selectedIndex;
		set {
			if (m_selectedIndex == value) return;
			m_selectedIndex = value;
			InvalidateVisual();
		}
	}

	// reorder insertion line
	public void SetDropLine(int index, int depth) {
		if (m_dropLineIndex == index && m_dropLineDepth == depth) return;
		m_dropLineIndex = index;
		m_dropLineDepth = depth;
		InvalidateVisual();
	}

	protected override Size MeasureOverride(Size availableSize) {
		return new Size(0, m_offsets[^1]);
		// width 0 = stretches to the grid cell
	}

	private static double ContentX(int depth) {
		return LeftPad + depth * IndentStep;
	}

	private static double CenterX(int depth) {
		return ContentX(depth) + SwatchHalf;
	}

	private double CenterY(int index) {
		return GetRowTop(index) + m_rows[index].VisualHeight / 2;
	}

	public override void Render(DrawingContext ctx) {
		DrawSelection(ctx);
		DrawDropLine(ctx);
		if (m_hoveredIndex >= 0 && m_hoveredIndex < m_rows.Count) {
			var last = m_hoveredIndex;
			while (last + 1 < m_rows.Count && m_rows[last + 1].Depth > m_rows[m_hoveredIndex].Depth) last++;
			var x = ContentX(m_rows[m_hoveredIndex].Depth) - BoxLeftPad;
			var rect = new Rect(x, GetRowTop(m_hoveredIndex), Math.Max(0, Bounds.Width - x - BoxRightPad), GetRowTop(last + 1) - GetRowTop(m_hoveredIndex));
			if (rect.Width > 2 && rect.Height > 2)
				ctx.DrawRectangle(null, new Pen(Resolve("Bg4", "#383637"), 2), new RoundedRect(rect.Deflate(1), 8));
		}
	}

	private void DrawSelection(DrawingContext ctx) {
		if (m_selectedIndex < 0 || m_selectedIndex >= m_rows.Count) return;

		var bg3 = Resolve("Bg3", "#2b2a2a");
		var bg4 = Resolve("Bg4", "#383637");

		var selDepth = m_rows[m_selectedIndex].Depth;

		var lastDesc = m_selectedIndex;
		for (var i = m_selectedIndex + 1; i < m_rows.Count && m_rows[i].Depth > selDepth; i++)
			lastDesc = i;

		// selection box
		var box = new Rect(
			ContentX(selDepth) - BoxLeftPad,
			GetRowTop(m_selectedIndex),
			Bounds.Width - (ContentX(selDepth) - BoxLeftPad) - BoxRightPad,
			GetRowTop(lastDesc + 1) - GetRowTop(m_selectedIndex));
		ctx.DrawRectangle(bg3, null, new RoundedRect(box, BoxCorner));

		// descendant connectors
		var descPen = new Pen(bg4, DescendantWidth) {
			LineCap = PenLineCap.Round, LineJoin = PenLineJoin.Round
		};
		for (var p = m_selectedIndex; p <= lastDesc; p++)
			DrawChildConnectors(ctx, descPen, p, lastDesc);

		// -ancestor path
		var ancPen = new Pen(bg3, AncestorWidth) {
			LineCap = PenLineCap.Round, LineJoin = PenLineJoin.Round
		};
		var child = m_selectedIndex;
		while (m_rows[child].Depth > 0) {
			var parent = ParentIndex(child);
			DrawElbow(ctx, ancPen, parent, child);
			child = parent;
		}
	}

	// Red insertion line shown while dragging to reorder
	private void DrawDropLine(DrawingContext ctx) {
		if (m_dropLineIndex < 0) return;

		var red = Resolve("Red", "#ff1659");
		var y = GetRowTop(m_dropLineIndex);
		var x0 = ContentX(m_dropLineDepth);

		var pen = new Pen(red, DropLineWidth) { LineCap = PenLineCap.Round };
		ctx.DrawLine(pen, new Point(x0 + DropDotRadius * 2, y), new Point(Bounds.Width - BoxRightPad, y));
		ctx.DrawEllipse(null, new Pen(red, DropLineWidth), new Point(x0 + DropDotRadius, y), DropDotRadius,
			DropDotRadius);
	}

	private void DrawChildConnectors(DrawingContext ctx, Pen pen, int parent, int limit) {
		var pd = m_rows[parent].Depth;
		var spineX = CenterX(pd);

		var lastChild = -1;
		for (var j = parent + 1; j <= limit && m_rows[j].Depth > pd; j++) {
			if (m_rows[j].Depth != pd + 1) continue; // direct children only
			var cy = CenterY(j);
			ctx.DrawLine(pen, new Point(spineX, cy), new Point(ContentX(pd + 1), cy));
			lastChild = j;
		}

		if (lastChild >= 0)
			ctx.DrawLine(pen, new Point(spineX, CenterY(parent)), new Point(spineX, CenterY(lastChild)));
	}

	private void DrawElbow(DrawingContext ctx, Pen pen, int parent, int child) {
		var spineX = CenterX(m_rows[parent].Depth);
		var cy = CenterY(child);
		ctx.DrawLine(pen, new Point(spineX, CenterY(parent)), new Point(spineX, cy));
		ctx.DrawLine(pen, new Point(spineX, cy), new Point(ContentX(m_rows[child].Depth), cy));
	}

	private int ParentIndex(int index) {
		var d = m_rows[index].Depth;
		for (var i = index - 1; i >= 0; i--)
			if (m_rows[i].Depth == d - 1)
				return i;
		return 0;
	}

	private IBrush Resolve(string key, string fallback) {
		if (Application.Current is { } app &&
		    app.TryGetResource(key, app.ActualThemeVariant, out var value) && value is IBrush brush)
			return brush;
		return new SolidColorBrush(Color.Parse(fallback));
	}
}
