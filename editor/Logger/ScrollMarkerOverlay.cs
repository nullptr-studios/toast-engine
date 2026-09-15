//
// ScrollMarkerOverlay.cs by Xein
// 28 Jul 2026
//

using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace editor.Logger;

public readonly record struct ScrollMarker(int DisplayIndex, string ColorKey);

public sealed class ScrollMarkerOverlay : Control {
	private const double MarkerWidth = 16;
	private const double MarkerHeight = 4;
	private const double MarkerCornerRadius = 2;
	private const double RowHeight = 24;

	public static readonly StyledProperty<IReadOnlyList<ScrollMarker>?> MarkersProperty =
		AvaloniaProperty.Register<ScrollMarkerOverlay, IReadOnlyList<ScrollMarker>?>(nameof(Markers));

	public static readonly StyledProperty<int> TotalCountProperty =
		AvaloniaProperty.Register<ScrollMarkerOverlay, int>(nameof(TotalCount));

	static ScrollMarkerOverlay() {
		AffectsRender<ScrollMarkerOverlay>(MarkersProperty, TotalCountProperty);
	}

	public IReadOnlyList<ScrollMarker>? Markers {
		get => GetValue(MarkersProperty);
		set => SetValue(MarkersProperty, value);
	}

	public int TotalCount {
		get => GetValue(TotalCountProperty);
		set => SetValue(TotalCountProperty, value);
	}

	public override void Render(DrawingContext context) {
		base.Render(context);

		var markers = Markers;
		var viewport = Bounds.Height; // shares the ListBox's own bounds
		if (markers is null || markers.Count == 0 || viewport <= 0 || TotalCount <= 0) return;

		var extent = TotalCount * RowHeight;
		var scrollableRange = extent - viewport;
		var thumbHeight = scrollableRange > 0 ? Math.Min(viewport, viewport * viewport / extent) : viewport;

		foreach (var marker in markers) {
			double y;
			if (scrollableRange > 0) {
				var offsetToShowAtTop = marker.DisplayIndex * RowHeight;
				var thumbFraction = Math.Clamp(offsetToShowAtTop / scrollableRange, 0.0, 1.0);
				y = thumbFraction * (viewport - thumbHeight);
			} else {
				// Everything already fits in the viewport
				y = TotalCount <= 1 ? 0 : (double)marker.DisplayIndex / TotalCount * viewport;
			}

			y = Math.Min(y, viewport - MarkerHeight);

			if (!this.TryFindResource(marker.ColorKey, out var resource) ||
			    resource is not IBrush brush)
				continue;

			var rect = new Rect(0, y, MarkerWidth, MarkerHeight);
			context.DrawRectangle(brush, null, rect, MarkerCornerRadius, MarkerCornerRadius);
		}
	}
}
