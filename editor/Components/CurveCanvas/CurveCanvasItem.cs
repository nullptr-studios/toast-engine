//
// CurveCanvasItem.cs by Xein
// 04 Jul 2026
//

using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets;

namespace editor.Components.CurveCanvas;

// One curve rendered by a CurveCanvas
// All point mutations funnel through here so the canvas and any point-list UI stay consistent
public sealed partial class CurveCanvasItem : ObservableObject {
	[ObservableProperty] private Color m_color;
	[ObservableProperty] private bool m_isEditable = true;
	[ObservableProperty] private bool m_isVisible = true;
	[ObservableProperty] private string m_label = "";

	public CurveCanvasItem(Curve curve, Color color, string label = "") {
		Curve = curve;
		m_color = color;
		m_label = label;
	}

	public Curve Curve { get; }

	// for legend swatches
	public IBrush ColorBrush => new SolidColorBrush(Color);

	public int NumPoints => Curve.NumPoints;
	public int MinPoints => Curve.SplineType == SplineType.Bezier ? 4 : 2;
	private int Dim => Curve.Dimension == CurveDimension.D2 ? 2 : 3;

	public event Action? Changed;

	partial void OnColorChanged(Color value) {
		OnPropertyChanged(nameof(ColorBrush));
	}

	// Bezier layout
	public bool IsHandle(int index) {
		return Curve.SplineType == SplineType.Bezier && index % 3 != 0;
	}

	public (float X, float Y) GetPoint(int index) {
		return (Curve.Points[index * Dim], Curve.Points[index * Dim + 1]);
	}

	public void MovePoint(int index, float x, float y) {
		if (index < 0 || index >= NumPoints) return;
		var dim = Dim;
		var pts = Curve.Points.ToArray();
		var dx = x - pts[index * dim + 0];
		var dy = y - pts[index * dim + 1];
		pts[index * dim + 0] = x;
		pts[index * dim + 1] = y;

		// Bezier anchors carry their tangent handles along, like any bezier editor
		if (Curve.SplineType == SplineType.Bezier && (NumPoints - 1) % 3 == 0 && index % 3 == 0) {
			if (index - 1 >= 0) {
				pts[(index - 1) * dim + 0] += dx;
				pts[(index - 1) * dim + 1] += dy;
			}

			if (index + 1 < NumPoints) {
				pts[(index + 1) * dim + 0] += dx;
				pts[(index + 1) * dim + 1] += dy;
			}
		}

		Curve.SetPoints(pts);
		Changed?.Invoke();
	}

	// Inserts at the X-sorted position and returns the new point's index
	public int InsertPoint(float x, float y) {
		var inserted = Curve.SplineType == SplineType.Bezier
			? InsertBezierAnchor(x, y)
			: InsertPlain(x, y);
		Changed?.Invoke();
		return inserted;
	}

	public bool RemovePoint(int index) {
		if (index < 0 || index >= NumPoints) return false;

		var removed = Curve.SplineType == SplineType.Bezier
			? RemoveBezier(index)
			: RemovePlain(index);

		if (removed) Changed?.Invoke();
		return removed;
	}

	// For structural edits made directly on the Curve
	public void NotifyStructureChanged() {
		Changed?.Invoke();
	}

	private int InsertPlain(float x, float y) {
		var dim = Dim;
		var pts = Curve.Points.ToList();
		var index = FindXOrderIndex(x);

		var comps = new List<float> { x, y };
		if (dim == 3) comps.Add(0f);
		pts.InsertRange(index * dim, comps);

		Curve.SetPoints(pts);
		return index;
	}

	private bool RemovePlain(int index) {
		if (NumPoints <= 2) return false;
		var dim = Dim;
		var pts = Curve.Points.ToList();
		pts.RemoveRange(index * dim, dim);
		Curve.SetPoints(pts);
		return true;
	}

	// Inserts an anchor plus its two handles so the count stays 3k+1
	private int InsertBezierAnchor(float x, float y) {
		var dim = Dim;
		var n = NumPoints;
		var pts = Curve.Points.ToList();

		// only operate on valid Bezier layouts; otherwise fall back to a plain insert
		if ((n - 1) % 3 != 0) return InsertPlainFromList(pts, x, y);

		var segments = (n - 1) / 3;
		var (firstX, _) = GetPoint(0);
		var (lastX, _) = GetPoint(n - 1);

		if (x <= firstX) {
			var (ax, ay) = (x, y);
			var (bx, by) = GetPoint(0);
			InsertComps(pts, 0, [
				ax, ay,
				Lerp(ax, bx, 1f / 3f), Lerp(ay, by, 1f / 3f),
				Lerp(ax, bx, 2f / 3f), Lerp(ay, by, 2f / 3f)
			], dim);
			Curve.SetPoints(pts);
			return 0;
		}

		if (x >= lastX) {
			var (ax, ay) = GetPoint(n - 1);
			var (bx, by) = (x, y);
			InsertComps(pts, n, [
				Lerp(ax, bx, 1f / 3f), Lerp(ay, by, 1f / 3f),
				Lerp(ax, bx, 2f / 3f), Lerp(ay, by, 2f / 3f),
				bx, by
			], dim);
			Curve.SetPoints(pts);
			return n + 2;
		}

		// interior
		var seg = 0;
		for (var s = 0; s < segments; s++) {
			var (a, _) = GetPoint(s * 3);
			var (b, _) = GetPoint(s * 3 + 3);
			if (x >= Math.Min(a, b) && x <= Math.Max(a, b)) {
				seg = s;
				break;
			}

			if (x > Math.Max(a, b)) seg = s;
		}

		var (prevX, prevY) = GetPoint(seg * 3);
		var (nextX, nextY) = GetPoint(seg * 3 + 3);
		InsertComps(pts, seg * 3 + 2, [
			Lerp(x, prevX, 1f / 3f), Lerp(y, prevY, 1f / 3f),
			x, y,
			Lerp(x, nextX, 1f / 3f), Lerp(y, nextY, 1f / 3f)
		], dim);
		Curve.SetPoints(pts);
		return seg * 3 + 3;
	}

	private bool RemoveBezier(int index) {
		var n = NumPoints;
		if ((n - 1) % 3 != 0) return RemovePlain(index);

		if (IsHandle(index)) {
			// Deleting a handle never breaks the layout: reset it to its neighbors' midpoint
			var (px, py) = GetPoint(index - 1);
			var (nx, ny) = GetPoint(index + 1);
			MovePoint(index, (px + nx) / 2f, (py + ny) / 2f);
			return false; // MovePoint already raised Changed
		}

		if (n <= 4) return false; // one segment left; keep the curve valid

		var dim = Dim;
		var pts = Curve.Points.ToList();
		var start = index == 0 ? 0
			: index == n - 1 ? n - 3
			: index - 1;
		pts.RemoveRange(start * dim, 3 * dim);
		Curve.SetPoints(pts);
		return true;
	}

	private int InsertPlainFromList(List<float> pts, float x, float y) {
		var dim = Dim;
		var index = FindXOrderIndex(x);
		var comps = new List<float> { x, y };
		if (dim == 3) comps.Add(0f);
		pts.InsertRange(index * dim, comps);
		Curve.SetPoints(pts);
		return index;
	}

	private int FindXOrderIndex(float x) {
		for (var i = 0; i < NumPoints; i++)
			if (GetPoint(i).X > x)
				return i;
		return NumPoints;
	}

	private static void InsertComps(List<float> pts, int pointIndex, float[] xy, int dim) {
		var comps = new List<float>(xy.Length / 2 * dim);
		for (var i = 0; i < xy.Length; i += 2) {
			comps.Add(xy[i]);
			comps.Add(xy[i + 1]);
			if (dim == 3) comps.Add(0f);
		}

		pts.InsertRange(pointIndex * dim, comps);
	}

	private static float Lerp(float a, float b, float t) {
		return a + (b - a) * t;
	}
}
