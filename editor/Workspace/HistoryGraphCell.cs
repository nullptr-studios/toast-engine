using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace editor.Workspace;

public sealed class HistoryGraphCell : Control {
	public const double LaneWidth = 14;
	public const double RowHeight = 34;
	private const double Pad = 9;

	public static readonly StyledProperty<HistoryRowViewModel?> RowProperty =
		AvaloniaProperty.Register<HistoryGraphCell, HistoryRowViewModel?>(nameof(Row));

	static HistoryGraphCell() {
		AffectsMeasure<HistoryGraphCell>(RowProperty);
		AffectsRender<HistoryGraphCell>(RowProperty);
	}

	public HistoryRowViewModel? Row {
		get => GetValue(RowProperty);
		set => SetValue(RowProperty, value);
	}

	protected override Size MeasureOverride(Size availableSize) {
		return new Size(Pad * 2 + Math.Max(1, Row?.GraphLaneCount ?? 1) * LaneWidth, RowHeight);
	}

	private static double X(int lane) {
		return Pad + lane * LaneWidth + LaneWidth / 2;
	}

	public override void Render(DrawingContext context) {
		if (Row is not { } row) return;
		var red = Resolve("Red", "#ff1659");
		var muted = Resolve("TextMuted", "#888888");
		var middle = Bounds.Height / 2;

		for (var i = 0; i < row.TopLanes.Count; i++) {
			var id = row.TopLanes[i];
			if (id == row.Id) continue;
			var bottomLane = IndexOf(row.BottomLanes, id);
			if (bottomLane < 0) continue;
			var active = !row.IsAboveCurrent && row.ActiveLineage.Contains(id);
			DrawCurve(context, PenFor(active ? red : muted),
				new Point(X(i), row.HasRowAbove ? 0 : middle),
				new Point(X(bottomLane), row.HasRowBelow ? Bounds.Height : middle));
		}

		if (row.HasRowAbove) {
			var activeTop = row.IsOnCurrentBranch && !row.IsCurrent;
			DrawCurve(context, PenFor(activeTop ? red : muted),
				new Point(X(row.Lane), 0), new Point(X(row.Lane), middle));
		}

		if (row.HasRowBelow)
			for (var parentIndex = 0; parentIndex < row.Parents.Count; parentIndex++) {
				var parent = row.Parents[parentIndex];
				var bottomLane = IndexOf(row.BottomLanes, parent);
				if (bottomLane < 0) continue;
				var active = row.IsOnCurrentBranch && parentIndex == 0 && row.ActiveLineage.Contains(parent);
				DrawCurve(context, PenFor(active ? red : muted),
					new Point(X(row.Lane), middle), new Point(X(bottomLane), Bounds.Height));
			}

		var brush = row.IsOnCurrentBranch ? red : muted;
		context.DrawEllipse(brush, new Pen(brush, row.IsCurrent ? 3 : 1.5),
			new Point(X(row.Lane), middle), row.IsCurrent ? 5 : 4, row.IsCurrent ? 5 : 4);
	}

	private static void DrawCurve(DrawingContext context, Pen pen, Point start, Point end) {
		if (start == end) return;
		var geometry = new StreamGeometry();
		using (var path = geometry.Open()) {
			path.BeginFigure(start, false);
			var bend = Math.Max(4, Math.Abs(end.Y - start.Y) * 0.58);
			path.CubicBezierTo(
				new Point(start.X, start.Y + bend),
				new Point(end.X, end.Y - bend),
				end);
			path.EndFigure(false);
		}

		context.DrawGeometry(null, pen, geometry);
	}

	private static int IndexOf(IReadOnlyList<ulong> lanes, ulong id) {
		for (var i = 0; i < lanes.Count; i++)
			if (lanes[i] == id)
				return i;
		return -1;
	}

	private static Pen PenFor(IBrush brush) {
		return new Pen(brush, 2) {
			LineCap = PenLineCap.Round,
			LineJoin = PenLineJoin.Round
		};
	}

	private static IBrush Resolve(string key, string fallback) {
		if (Application.Current is { } app &&
		    app.TryGetResource(key, app.ActualThemeVariant, out var value) && value is IBrush brush)
			return brush;
		return new SolidColorBrush(Color.Parse(fallback));
	}
}
