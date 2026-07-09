//
// Curve.cs by Xein
// 21 Jun 2026
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using TinySpline;
using Tomlyn;
using Tomlyn.Model;
using Tomlyn.Serialization;

namespace editor.Assets;

public enum SplineType {
	Linear,
	CatmullRom,
	BSpline,
	Bezier
}

public enum CurveDimension {
	D2,
	D3
}

public sealed class Curve {
	private float[] m_points = [];
	private BSpline m_spline = null!;

	public Curve(IReadOnlyList<float> points, CurveDimension dim, SplineType type, float tScale = 1f) {
		Dimension = dim;
		SplineType = type;
		TScale = tScale;
		m_points = points.ToArray();
		RebuildSpline();
	}

	public CurveDimension Dimension { get; }
	public SplineType SplineType { get; private set; }
	public float TScale { get; private set; }

	public IReadOnlyList<float> Points => m_points;
	public int NumPoints => m_points.Length / DimCount;
	private int DimCount => Dimension == CurveDimension.D2 ? 2 : 3;

	public static Curve FromFile(string path) {
		return FromString(File.ReadAllText(path));
	}

	public static Curve FromString(string toml) {
		var dto = TomlSerializer.Deserialize<CurveDto>(toml)
			?? throw new FormatException("Curve: failed to parse TOML");

		var dim = dto.Dimension == "3d" ? CurveDimension.D3 : CurveDimension.D2;
		var type = ParseSplineType(dto.SplineType);
		var nComp = dim == CurveDimension.D2 ? 2 : 3;

		var points = new List<float>((dto.Points?.Count ?? 0) * nComp);
		foreach (var pt in dto.Points ?? []) {
			points.Add(pt.X);
			points.Add(pt.Y);
			if (dim == CurveDimension.D3)
				points.Add(pt.Z);
		}

		return new Curve(points, dim, type, dto.TScale);
	}

	public void Save(string path) {
		File.WriteAllText(path, Serialize());
	}

	public string Serialize() {
		var dim = DimCount;
		var n = m_points.Length / dim;

		var dto = new CurveDto {
			SplineType = SplineTypeToString(SplineType),
			Dimension = Dimension == CurveDimension.D2 ? "2d" : "3d",
			TScale = TScale,
			Points = Enumerable.Range(0, n).Select(i => new PointDto {
				X = m_points[i * dim + 0],
				Y = m_points[i * dim + 1],
				Z = dim == 3 ? m_points[i * dim + 2] : 0f
			}).ToList()
		};

		return TomlSerializer.Serialize(dto);
	}

	public (float x, float y) Eval2D(float t) {
		var u = Math.Clamp(t / TScale, 0.0, 1.0);
		var v = m_spline.Eval(u).ResultVec2();
		return ((float)v.X, (float)v.Y);
	}

	public (float x, float y, float z) Eval3D(float t) {
		var u = Math.Clamp(t / TScale, 0.0, 1.0);
		var v = m_spline.Eval(u).ResultVec3();
		return ((float)v.X, (float)v.Y, (float)v.Z);
	}

	public void SetPoints(IReadOnlyList<float> points) {
		m_points = points.ToArray();
		RebuildSpline();
	}

	public void SetSplineType(SplineType type) {
		SplineType = type;
		RebuildSpline();
	}

	public void SetTScale(float tScale) {
		TScale = MathF.Max(tScale, 1e-4f);
	}

	public Curve Clone() {
		return new Curve(m_points, Dimension, SplineType, TScale);
	}

	public static Curve FromToml(TomlTable table) {
		var dim = table.TryGetValue("dimension", out var d) && d as string == "3d"
			? CurveDimension.D3
			: CurveDimension.D2;
		var type = ParseSplineType(table.TryGetValue("spline_type", out var st) ? st as string ?? "" : "");
		var tScale = table.TryGetValue("t_scale", out var ts) ? ToFloat(ts, 1f) : 1f;

		var points = new List<float>();
		if (table.TryGetValue("points", out var p) && p is TomlTableArray pts)
			foreach (var pt in pts) {
				points.Add(pt.TryGetValue("x", out var x) ? ToFloat(x, 0f) : 0f);
				points.Add(pt.TryGetValue("y", out var y) ? ToFloat(y, 0f) : 0f);
				if (dim == CurveDimension.D3)
					points.Add(pt.TryGetValue("z", out var z) ? ToFloat(z, 0f) : 0f);
			}

		return new Curve(points, dim, type, tScale);
	}

	public TomlTable ToToml() {
		var dim = DimCount;
		var n = NumPoints;

		var pts = new TomlTableArray();
		for (var i = 0; i < n; i++) {
			var pt = new TomlTable {
				["x"] = (double)m_points[i * dim + 0],
				["y"] = (double)m_points[i * dim + 1]
			};
			if (dim == 3)
				pt["z"] = (double)m_points[i * dim + 2];
			pts.Add(pt);
		}

		return new TomlTable {
			["spline_type"] = SplineTypeToString(SplineType),
			["dimension"] = Dimension == CurveDimension.D2 ? "2d" : "3d",
			["t_scale"] = (double)TScale,
			["points"] = pts
		};
	}

	private static float ToFloat(object? v, float fallback) {
		return v switch {
			double d => (float)d,
			float f => f,
			long l => l,
			int i => i,
			_ => fallback
		};
	}

	private void RebuildSpline() {
		var dim = DimCount;
		var n = m_points.Length / dim;

		if (n < 2)
			throw new InvalidOperationException($"Curve needs at least 2 control points; got {n}");

		IList<double> pts = m_points.Select(v => (double)v).ToList();

		m_spline = SplineType switch {
			SplineType.Linear =>
				Build(pts, (uint)n, (uint)dim, 1, BSpline.Type.Clamped),

			SplineType.BSpline =>
				Build(pts, (uint)n, (uint)dim, Math.Min(3u, (uint)n - 1), BSpline.Type.Clamped),

			SplineType.Bezier when (n - 1) % 3 == 0 && n >= 4 =>
				BuildBeziers(pts, n, dim),

			SplineType.Bezier =>
				Build(pts, (uint)n, (uint)dim, Math.Min(3u, (uint)n - 1), BSpline.Type.Clamped),

			SplineType.CatmullRom =>
				BSpline.InterpolateCatmullRom(pts, (uint)dim),

			_ => throw new InvalidOperationException("Unknown SplineType")
		};
	}

	private static BSpline Build(
		IList<double> pts, uint n, uint dim, uint degree, BSpline.Type type) {
		var spline = new BSpline(n, dim, degree, type);
		spline.ControlPoints = pts;
		return spline;
	}

	private static BSpline BuildBeziers(IList<double> pts, int n, int dim) {
		var segments = (n - 1) / 3;
		var expanded = new List<double>(segments * 4 * dim);
		for (var s = 0; s < segments; s++)
		for (var p = 0; p <= 3; p++)
		for (var d = 0; d < dim; d++)
			expanded.Add(pts[(s * 3 + p) * dim + d]);
		return Build(expanded, (uint)(segments * 4), (uint)dim, 3, BSpline.Type.Beziers);
	}

	private static string SplineTypeToString(SplineType t) {
		return t switch {
			SplineType.Linear => "linear",
			SplineType.CatmullRom => "catmull_rom",
			SplineType.BSpline => "bspline",
			SplineType.Bezier => "bezier",
			_ => "linear"
		};
	}

	private static SplineType ParseSplineType(string s) {
		return s switch {
			"catmull_rom" => SplineType.CatmullRom,
			"bspline" => SplineType.BSpline,
			"bezier" => SplineType.Bezier,
			_ => SplineType.Linear
		};
	}
}

file sealed class CurveDto {
	[TomlPropertyName("spline_type")] public string SplineType { get; set; } = "linear";
	[TomlPropertyName("dimension")] public string Dimension { get; set; } = "2d";
	[TomlPropertyName("t_scale")] public float TScale { get; set; } = 1f;
	[TomlPropertyName("points")] public List<PointDto> Points { get; set; } = [];
}

file sealed class PointDto {
	[TomlPropertyName("x")] public float X { get; set; }
	[TomlPropertyName("y")] public float Y { get; set; }
	[TomlPropertyName("z")] public float Z { get; set; }
}
