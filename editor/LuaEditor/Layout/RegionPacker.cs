using System;
using System.Collections.Generic;
using System.Linq;

namespace editor.LuaEditor.Layout;

public static class RegionPacker {
	public const double Gap = 80;
	public const double TargetAspect = 16.0 / 9.0;

	public static List<Vec> Pack(IReadOnlyList<Size> sizes, IReadOnlyList<Rect> obstacles, double aspect = TargetAspect) {
		var widest = sizes.Count == 0 ? 0 : sizes.Max(s => s.W);
		var total = sizes.Sum(s => s.W + Gap);
		var area = sizes.Sum(s => (s.W + Gap) * (s.H + Gap));
		var candidates = new SortedSet<double> { Math.Max(widest, Math.Sqrt(area * aspect)), widest };
		for (var k = 2; k <= 6; k++) candidates.Add(Math.Max(widest, widest * k + Gap * (k - 1)));
		candidates.Add(Math.Max(widest, total));

		List<Vec>? best = null;
		var bestScore = double.MaxValue;
		foreach (var rowWidth in candidates) {
			var positions = PackAt(sizes, obstacles, rowWidth);
			var right = 0.0;
			var bottom = 0.0;
			for (var i = 0; i < sizes.Count; i++) {
				right = Math.Max(right, positions[i].X + sizes[i].W);
				bottom = Math.Max(bottom, positions[i].Y + sizes[i].H);
			}
			var shape = bottom <= 0 || right <= 0 ? 1 : right / bottom;
			var score = Math.Abs(Math.Log(shape / aspect)) + 0.0000001 * right * bottom;
			if (score < bestScore) {
				bestScore = score;
				best = positions;
			}
		}
		return best ?? [];
	}

	private static List<Vec> PackAt(IReadOnlyList<Size> sizes, IReadOnlyList<Rect> obstacles, double rowWidth) {
		var result = new List<Vec>();
		var x = 0.0;
		var y = 0.0;
		var rowHeight = 0.0;
		foreach (var size in sizes) {
			while (true) {
				if (x > 0 && x + size.W > rowWidth) {
					x = 0;
					y += rowHeight + Gap;
					rowHeight = 0;
				}
				var candidate = new Rect(x, y, size.W, size.H);
				var hit = obstacles.Where(o => o.Inflate(Gap / 2).Intersects(candidate)).Select(o => (Rect?)o).FirstOrDefault();
				if (hit is null) break;
				if (x == 0 && hit.Value.Right + Gap + size.W > rowWidth) {
					y = hit.Value.Bottom + Gap;
					rowHeight = 0;
					continue;
				}
				x = hit.Value.Right + Gap;
			}
			result.Add(new Vec(x, y));
			x += size.W + Gap;
			rowHeight = Math.Max(rowHeight, size.H);
		}
		return result;
	}

	public static int LifecycleRank(string functionName) => functionName switch {
		"init" or "awake" or "start" => 0,
		"tick" or "update" or "fixedTick" or "lateUpdate" => 1,
		"destroy" or "onDestroy" => 2,
		_ => 3
	};
}
