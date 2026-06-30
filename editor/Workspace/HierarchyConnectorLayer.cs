using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace editor.Workspace;

// You dont even know how much this took -x
public sealed class HierarchyConnectorLayer : Control {
	public const double RowHeight = 28;
	public const double IndentStep = 20;
	public const double LeftPad = 10;   // left inset of the whole tree
	public const double SwatchHalf = 8;

	private const double BoxLeftPad = 6;
	private const double BoxRightPad = 8;
	private const double BoxCorner = 8;
	private const double AncestorWidth = 3;
	private const double DescendantWidth = 2;
	private const double DropLineWidth = 2;
	private const double DropDotRadius = 3.5;

	private IReadOnlyList<HierarchyElement> m_rows = [];
	private int m_selectedIndex = -1;
	private int m_dropLineIndex = -1; // row boundary to draw the insertion line at; -1 = none
	private int m_dropLineDepth;

	public IReadOnlyList<HierarchyElement> Rows {
		get => m_rows;
		set {
			m_rows = value;
			InvalidateMeasure();
			InvalidateVisual();
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

	protected override Size MeasureOverride(Size availableSize) =>
		new(0, m_rows.Count * RowHeight); // width 0 = stretches to the grid cell

	private static double ContentX(int depth) => LeftPad + depth * IndentStep;
	private static double CenterX(int depth) => ContentX(depth) + SwatchHalf;
	private static double CenterY(int index) => index * RowHeight + RowHeight / 2;

	public override void Render(DrawingContext ctx) {
		DrawSelection(ctx);
		DrawDropLine(ctx);
	}

	private void DrawSelection(DrawingContext ctx) {
		if (m_selectedIndex < 0 || m_selectedIndex >= m_rows.Count) return;

		var bg3 = Resolve("Bg3", "#2b2a2a");
		var bg4 = Resolve("Bg4", "#383637");

		int selDepth = m_rows[m_selectedIndex].Depth;

		int lastDesc = m_selectedIndex;
		for (int i = m_selectedIndex + 1; i < m_rows.Count && m_rows[i].Depth > selDepth; i++)
			lastDesc = i;

		// selection box
		var box = new Rect(
			ContentX(selDepth) - BoxLeftPad,
			m_selectedIndex * RowHeight,
			Bounds.Width - (ContentX(selDepth) - BoxLeftPad) - BoxRightPad,
			(lastDesc - m_selectedIndex + 1) * RowHeight);
		ctx.DrawRectangle(bg3, null, new RoundedRect(box, BoxCorner));

		// descendant connectors
		var descPen = new Pen(bg4, DescendantWidth) {
			LineCap = PenLineCap.Round, LineJoin = PenLineJoin.Round
		};
		for (int p = m_selectedIndex; p <= lastDesc; p++)
			DrawChildConnectors(ctx, descPen, p, lastDesc);

		// -ancestor path
		var ancPen = new Pen(bg3, AncestorWidth) {
			LineCap = PenLineCap.Round, LineJoin = PenLineJoin.Round
		};
		int child = m_selectedIndex;
		while (m_rows[child].Depth > 0) {
			int parent = ParentIndex(child);
			DrawElbow(ctx, ancPen, parent, child);
			child = parent;
		}
	}

	// Red insertion line shown while dragging to reorder
	private void DrawDropLine(DrawingContext ctx) {
		if (m_dropLineIndex < 0) return;

		var red = Resolve("Red", "#ff1659");
		double y = m_dropLineIndex * RowHeight;
		double x0 = ContentX(m_dropLineDepth);

		var pen = new Pen(red, DropLineWidth) { LineCap = PenLineCap.Round };
		ctx.DrawLine(pen, new Point(x0 + DropDotRadius * 2, y), new Point(Bounds.Width - BoxRightPad, y));
		ctx.DrawEllipse(null, new Pen(red, DropLineWidth), new Point(x0 + DropDotRadius, y), DropDotRadius, DropDotRadius);
	}

	private void DrawChildConnectors(DrawingContext ctx, Pen pen, int parent, int limit) {
		int pd = m_rows[parent].Depth;
		double spineX = CenterX(pd);

		int lastChild = -1;
		for (int j = parent + 1; j <= limit && m_rows[j].Depth > pd; j++) {
			if (m_rows[j].Depth != pd + 1) continue; // direct children only
			double cy = CenterY(j);
			ctx.DrawLine(pen, new Point(spineX, cy), new Point(ContentX(pd + 1), cy));
			lastChild = j;
		}

		if (lastChild >= 0)
			ctx.DrawLine(pen, new Point(spineX, CenterY(parent)), new Point(spineX, CenterY(lastChild)));
	}

	private void DrawElbow(DrawingContext ctx, Pen pen, int parent, int child) {
		double spineX = CenterX(m_rows[parent].Depth);
		double cy = CenterY(child);
		ctx.DrawLine(pen, new Point(spineX, CenterY(parent)), new Point(spineX, cy));
		ctx.DrawLine(pen, new Point(spineX, cy), new Point(ContentX(m_rows[child].Depth), cy));
	}

	private int ParentIndex(int index) {
		int d = m_rows[index].Depth;
		for (int i = index - 1; i >= 0; i--)
			if (m_rows[i].Depth == d - 1) return i;
		return 0;
	}

	private IBrush Resolve(string key, string fallback) {
		if (Application.Current is { } app &&
		    app.TryGetResource(key, app.ActualThemeVariant, out var value) && value is IBrush brush)
			return brush;
		return new SolidColorBrush(Color.Parse(fallback));
	}
}
