using System;
using System.Diagnostics;
using Avalonia.Threading;

namespace editor.Components.Elements;

public static class CollapseAnimator {
	public const int DurationMs = 180;

	public static DispatcherTimer Run(double from, double to, Action<double> apply, Action? done = null) {
		var clock = Stopwatch.StartNew();
		var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(8) };
		timer.Tick += (_, _) => {
			var t = Math.Min(clock.ElapsedMilliseconds / (double)DurationMs, 1.0);
			if (t >= 1) {
				timer.Stop();
				apply(to);
				done?.Invoke();
				return;
			}
			apply(from + (to - from) * (1 - Math.Pow(1 - t, 3)));
		};
		timer.Start();
		return timer;
	}
}
