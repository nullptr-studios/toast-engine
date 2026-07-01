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
using Tomlyn.Serialization;

namespace editor.Assets;

public enum SplineType {
	Linear,
	CatmullRom,
	BSpline,
	Bezier,
}

public enum CurveDimension {
	D2,
	D3,
}

public sealed class Curve {
	private TinySpline.BSpline m_spline = null!;
	private float[] m_points = [];

	public CurveDimension Dimension { get; private set; }
	public SplineType SplineType { get; private set; }
	public float TScale { get; private set; }

	public IReadOnlyList<float> Points => m_points;
	public int NumPoints => m_points.Length / DimCount;
	private int DimCount => Dimension == CurveDimension.D2 ? 2 : 3;

	public Curve(IReadOnlyList<float> points, CurveDimension dim, SplineType type, float tScale = 1f) {
		Dimension = dim;
		SplineType = type;
		TScale = tScale;
		m_points = points.ToArray();
		RebuildSpline();
	}

	public static Curve FromFile(string path) => FromString(File.ReadAllText(path));

	public static Curve FromString(string toml) {
		var dto = TomlSerializer.Deserialize<CurveDto>(toml)
		          ?? throw new FormatException("Curve: failed to parse TOML");

		var dim = dto.Dimension == "3d" ? CurveDimension.D3 : CurveDimension.D2;
		var type = ParseSplineType(dto.SplineType);
		int nComp = dim == CurveDimension.D2 ? 2 : 3;

		var points = new List<float>((dto.Points?.Count ?? 0) * nComp);
		foreach (var pt in dto.Points ?? []) {
			points.Add(pt.X);
			points.Add(pt.Y);
			if (dim == CurveDimension.D3)
				points.Add(pt.Z);
		}

		return new Curve(points, dim, type, dto.TScale);
	}

	public void Save(string path) => File.WriteAllText(path, Serialize());

	public string Serialize() {
		int dim = DimCount;
		int n = m_points.Length / dim;

		var dto = new CurveDto {
			SplineType = SplineTypeToString(SplineType),
			Dimension = Dimension == CurveDimension.D2 ? "2d" : "3d",
			TScale = TScale,
			Points = Enumerable.Range(0, n).Select(i => new PointDto {
				X = m_points[i * dim + 0],
				Y = m_points[i * dim + 1],
				Z = dim == 3 ? m_points[i * dim + 2] : 0f,
			}).ToList(),
		};

		return TomlSerializer.Serialize(dto);
	}

	public (float x, float y) Eval2D(float t) {
		double u = Math.Clamp((double)(t / TScale), 0.0, 1.0);
		var v = m_spline.Eval(u).ResultVec2();
		return ((float)v.X, (float)v.Y);
	}

	public (float x, float y, float z) Eval3D(float t) {
		double u = Math.Clamp((double)(t / TScale), 0.0, 1.0);
		var v = m_spline.Eval(u).ResultVec3();
		return ((float)v.X, (float)v.Y, (float)v.Z);
	}

	public void SetPoints(IReadOnlyList<float> points) {
		m_points = points.ToArray();
		RebuildSpline();
	}

	private void RebuildSpline() {
		int dim = DimCount;
		int n = m_points.Length / dim;

		if (n < 2)
			throw new InvalidOperationException($"Curve needs at least 2 control points; got {n}");

		IList<double> pts = m_points.Select(v => (double)v).ToList();

		m_spline = SplineType switch {
			SplineType.Linear =>
				Build(pts, (uint)n, (uint)dim, 1, TinySpline.BSpline.Type.Clamped),

			SplineType.BSpline =>
				Build(pts, (uint)n, (uint)dim, Math.Min(3u, (uint)n - 1), TinySpline.BSpline.Type.Clamped),

			SplineType.Bezier when (n - 1) % 3 == 0 =>
				Build(pts, (uint)n, (uint)dim, 3, TinySpline.BSpline.Type.Beziers),

			SplineType.Bezier =>
				Build(pts, (uint)n, (uint)dim, Math.Min(3u, (uint)n - 1), TinySpline.BSpline.Type.Clamped),

			SplineType.CatmullRom =>
				TinySpline.BSpline.InterpolateCatmullRom(pts, (uint)dim),

			_ => throw new InvalidOperationException("Unknown SplineType")
		};
	}

	private static TinySpline.BSpline Build(
		IList<double> pts, uint n, uint dim, uint degree, TinySpline.BSpline.Type type) {
		var spline = new TinySpline.BSpline(n, dim, degree, type);
		spline.ControlPoints = pts;
		return spline;
	}

	private static string SplineTypeToString(SplineType t) =>
		t switch {
			SplineType.Linear => "linear",
			SplineType.CatmullRom => "catmull_rom",
			SplineType.BSpline => "bspline",
			SplineType.Bezier => "bezier",
			_ => "linear"
		};

	private static SplineType ParseSplineType(string s) =>
		s switch {
			"catmull_rom" => SplineType.CatmullRom,
			"bspline" => SplineType.BSpline,
			"bezier" => SplineType.Bezier,
			_ => SplineType.Linear
		};
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
