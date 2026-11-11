using System;
using System.Diagnostics;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;

namespace editor.Components.Elements;

public sealed class AnimatedReveal : Decorator {
	public static readonly StyledProperty<bool> IsExpandedProperty =
		AvaloniaProperty.Register<AnimatedReveal, bool>(nameof(IsExpanded), true);
	private DispatcherTimer? m_timer;
	private double? m_height;
	public bool IsExpanded { get => GetValue(IsExpandedProperty); set => SetValue(IsExpandedProperty, value); }
	public AnimatedReveal() { ClipToBounds = true; }

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property != IsExpandedProperty) return;
		m_timer?.Stop();
		var child = Child;
		if (child is null || TopLevel.GetTopLevel(this) is null) { m_height = null; InvalidateMeasure(); return; }
		var from = Bounds.Height;
		child.IsVisible = true;
		child.Measure(new Size(Bounds.Width, double.PositiveInfinity));
		var to = IsExpanded ? child.DesiredSize.Height : 0;
		m_height = from;
		var clock = Stopwatch.StartNew();
		var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(8) };
		m_timer = timer;
		timer.Tick += (_, _) => {
			var t = Math.Min(clock.Elapsed.TotalMilliseconds / 180, 1);
			m_height = from + (to - from) * (1 - Math.Pow(1 - t, 3));
			if (t >= 1) {
				timer.Stop(); m_timer = null; m_height = null;
				if (Child is not null) Child.IsVisible = IsExpanded;
			}
			InvalidateMeasure();
		};
		timer.Start();
		InvalidateMeasure();
	}

	protected override Size MeasureOverride(Size availableSize) {
		if (Child is null) return default;
		if (m_timer is null) Child.IsVisible = IsExpanded;
		Child.Measure(new Size(availableSize.Width, double.PositiveInfinity));
		return new Size(Child.DesiredSize.Width, m_height ?? (IsExpanded ? Child.DesiredSize.Height : 0));
	}

	protected override Size ArrangeOverride(Size finalSize) {
		Child?.Arrange(new Rect(0, 0, finalSize.Width, Math.Max(finalSize.Height, Child.DesiredSize.Height)));
		return finalSize;
	}

	protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
		m_timer?.Stop(); m_timer = null; m_height = null;
		base.OnDetachedFromVisualTree(e);
	}
}
