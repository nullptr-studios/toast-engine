using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;

namespace editor.Components.Elements;

public sealed class SpanPanel : Panel {
	public static readonly StyledProperty<int> ColumnsProperty =
		AvaloniaProperty.Register<SpanPanel, int>(nameof(Columns), 3);

	public static readonly StyledProperty<double> SpacingProperty =
		AvaloniaProperty.Register<SpanPanel, double>(nameof(Spacing), 8);

	public static readonly AttachedProperty<int> SpanProperty =
		AvaloniaProperty.RegisterAttached<SpanPanel, Control, int>("Span", 1);

	static SpanPanel() {
		AffectsMeasure<SpanPanel>(ColumnsProperty, SpacingProperty);
		SpanProperty.Changed.AddClassHandler<Control>((c, _) => (c.Parent as Panel)?.InvalidateMeasure());
	}

	public int Columns {
		get => GetValue(ColumnsProperty);
		set => SetValue(ColumnsProperty, value);
	}

	public double Spacing {
		get => GetValue(SpacingProperty);
		set => SetValue(SpacingProperty, value);
	}

	public static int GetSpan(Control control) => control.GetValue(SpanProperty);
	public static void SetSpan(Control control, int value) => control.SetValue(SpanProperty, value);

	private readonly List<(Control Child, int Row, int Column, int Span)> m_placed = [];

	private double CellWidth(double width) {
		var columns = Math.Max(1, Columns);
		return double.IsInfinity(width) ? 0 : Math.Max(0, (width - (columns - 1) * Spacing) / columns);
	}

	/// first row and column where the child fits
	private void Place() {
		m_placed.Clear();
		var columns = Math.Max(1, Columns);
		var used = new List<bool[]>();
		foreach (var child in Children) {
			if (!child.IsVisible) continue;
			var span = Math.Clamp(GetSpan(child), 1, columns);
			var placed = false;
			for (var row = 0; !placed; row++) {
				if (row == used.Count) used.Add(new bool[columns]);
				for (var column = 0; column + span <= columns && !placed; column++) {
					var free = true;
					for (var i = column; i < column + span; i++) free &= !used[row][i];
					if (!free) continue;
					for (var i = column; i < column + span; i++) used[row][i] = true;
					m_placed.Add((child, row, column, span));
					placed = true;
				}
			}
		}
	}

	private double[] RowHeights() {
		var rows = 0;
		foreach (var p in m_placed) rows = Math.Max(rows, p.Row + 1);
		var heights = new double[rows];
		foreach (var p in m_placed) heights[p.Row] = Math.Max(heights[p.Row], p.Child.DesiredSize.Height);
		return heights;
	}

	protected override Size MeasureOverride(Size available) {
		Place();
		var cell = CellWidth(available.Width);
		foreach (var p in m_placed)
			p.Child.Measure(new Size(cell * p.Span + (p.Span - 1) * Spacing, double.PositiveInfinity));
		var heights = RowHeights();
		var total = 0.0;
		foreach (var h in heights) total += h;
		if (heights.Length > 1) total += (heights.Length - 1) * Spacing;
		return new Size(double.IsInfinity(available.Width) ? 0 : available.Width, total);
	}

	protected override Size ArrangeOverride(Size finalSize) {
		var cell = CellWidth(finalSize.Width);
		var heights = RowHeights();
		var tops = new double[heights.Length];
		var y = 0.0;
		for (var i = 0; i < heights.Length; i++) {
			tops[i] = y;
			y += heights[i] + Spacing;
		}
		foreach (var p in m_placed) {
			var width = cell * p.Span + (p.Span - 1) * Spacing;
			p.Child.Arrange(new Rect(p.Column * (cell + Spacing), tops[p.Row], width, heights[p.Row]));
		}
		return finalSize;
	}
}
