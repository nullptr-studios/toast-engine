using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;

namespace editor.VoxelEditor;

public sealed class VoxelViewPanel : Panel {
	private const double Gap = 4;
	private double m_sideSplit = 0.5;
	private double m_topSplit = 0.5;

	public static readonly StyledProperty<bool> FourUpProperty =
		AvaloniaProperty.Register<VoxelViewPanel, bool>(nameof(FourUp));

	static VoxelViewPanel() {
		AffectsMeasure<VoxelViewPanel>(FourUpProperty);
	}

	public VoxelViewPanel() {
		ClipToBounds = true;
		AddHandler(Thumb.DragDeltaEvent, OnDividerDrag);
	}

	public bool FourUp {
		get => GetValue(FourUpProperty);
		set => SetValue(FourUpProperty, value);
	}

	private void OnDividerDrag(object? sender, VectorEventArgs e) {
		if (!FourUp || Bounds.Width <= 0 || Bounds.Height <= 0) return;
		switch ((e.Source as Control)?.Name) {
			case "ViewsColumnSplitter":
				m_sideSplit = Math.Clamp(m_sideSplit + e.Vector.X / Bounds.Width, 0.15, 0.85);
				break;
			case "ViewsRowSplitter":
				m_topSplit = Math.Clamp(m_topSplit + e.Vector.Y / Bounds.Height, 0.15, 0.85);
				break;
			default: return;
		}
		InvalidateMeasure();
		e.Handled = true;
	}

	private Rect Frame(Control child, Size size) {
		var halfWidth = size.Width / 2;
		var halfHeight = size.Height / 2;
		var side = size.Width * m_sideSplit;
		var top = size.Height * m_topSplit;
		var inset = Gap / 2;
		return child.Name switch {
			"FrontViewHost" => new Rect(0, 0, Math.Max(0, Math.Max(halfWidth, side) - inset), Math.Max(0, Math.Max(halfHeight, top) - inset)),
			"SideViewHost" => new Rect(side + inset, 0, Math.Max(0, size.Width - side - inset), Math.Max(0, halfHeight - inset)),
			"TopViewHost" => new Rect(0, top + inset, Math.Max(0, halfWidth - inset), Math.Max(0, size.Height - top - inset)),
			"ViewportHost" when FourUp => new Rect(halfWidth + inset, halfHeight + inset, Math.Max(0, halfWidth - inset), Math.Max(0, halfHeight - inset)),
			"ViewportHost" => new Rect(size),
			"ViewsColumnSplitter" => new Rect(side - inset, 0, Gap, Math.Min(top, halfHeight)),
			"ViewsRowSplitter" => new Rect(0, top - inset, Math.Min(side, halfWidth), Gap),
			_ => default
		};
	}

	protected override Size MeasureOverride(Size availableSize) {
		var size = new Size(double.IsFinite(availableSize.Width) ? availableSize.Width : 0,
			double.IsFinite(availableSize.Height) ? availableSize.Height : 0);
		foreach (var child in Children) child.Measure(Frame(child, size).Size);
		return size;
	}

	protected override Size ArrangeOverride(Size finalSize) {
		foreach (var child in Children) child.Arrange(Frame(child, finalSize));
		return finalSize;
	}
}
