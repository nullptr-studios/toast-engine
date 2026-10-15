//
// Curve.cs by Xein
// 21 Jun 2026
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using editor.Engine;
using TinySpline;
using Tomlyn;
using Tomlyn.Model;

namespace editor.Assets;

public enum SplineType {
	Linear,
	CatmullRom,
	BSpline,
	Bezier
}

public enum CurveDimension {
	D1 = 1,
	D2 = 2,
	D3 = 3
}

public sealed class Curve {
	private float[] m_points = [];
	private BSpline m_spline = null!;

	public Curve(IReadOnlyList<float> points, SplineType type, float tScale = 1f) {
		SplineType = type;
		TScale = tScale;
		m_points = Sanitize(points.ToArray());
		RebuildSpline();
	}

	public SplineType SplineType { get; private set; }
	public float TScale { get; private set; }

	public IReadOnlyList<float> Points => m_points;
	public int NumPoints => m_points.Length / 2;

	public (float x, float y) Eval2D(float t) {
		var u = Math.Clamp(t / TScale, 0.0, 1.0);
		var v = m_spline.Eval(u).ResultVec2();
		return ((float)v.X, (float)v.Y);
	}

	public float EvalAtX(float x) {
		x = Math.Clamp(x, 0f, 1f);
		try {
			return (float)m_spline.Bisect(x).ResultVec2().Y;
		} catch (Exception) {
			return Eval2D(x * TScale).y;
		}
	}

	public void SetPoints(IReadOnlyList<float> points) {
		m_points = Sanitize(points.ToArray());
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
		return new Curve(m_points, SplineType, TScale);
	}

	public static Curve FromToml(TomlTable table) {
		var type = ParseSplineType(table.TryGetValue("spline_type", out var st) ? st as string ?? "" : "");
		var tScale = table.TryGetValue("t_scale", out var ts) ? ToFloat(ts, 1f) : 1f;
		var points = ReadPoints(table, "points");
		return new Curve(points, type, tScale * NormalizeX(points));
	}

	public TomlTable ToToml() {
		return new TomlTable {
			["spline_type"] = SplineTypeToString(SplineType),
			["dimension"] = "1d",
			["t_scale"] = (double)TScale,
			["points"] = WritePoints()
		};
	}

	internal static float[] Sanitize(float[] points) {
		var count = points.Length / 2;
		if (count < 2) return points;
		var previous = 0f;
		for (var i = 0; i < count; i++)
			points[i * 2] = previous = Math.Clamp(points[i * 2], previous, 1f);
		points[0] = 0f;
		points[(count - 1) * 2] = 1f;
		return points;
	}

	internal static float NormalizeX(List<float> points) {
		if (points.Count < 4) return 1f;
		var first = points[0];
		var range = points[^2] - first;
		if (range <= 1e-6f) return 1f;
		for (var i = 0; i + 1 < points.Count; i += 2) points[i] = (points[i] - first) / range;
		return range;
	}

	internal static List<float> ReadPoints(TomlTable table, string key) {
		var points = new List<float>();
		if (table.TryGetValue(key, out var p) && p is TomlTableArray pts)
			foreach (var pt in pts) {
				points.Add(pt.TryGetValue("x", out var x) ? ToFloat(x, 0f) : 0f);
				points.Add(pt.TryGetValue("y", out var y) ? ToFloat(y, 0f) : 0f);
			}
		return points;
	}

	internal TomlTableArray WritePoints() {
		var pts = new TomlTableArray();
		for (var i = 0; i < NumPoints; i++)
			pts.Add(new TomlTable {
				["x"] = (double)m_points[i * 2 + 0],
				["y"] = (double)m_points[i * 2 + 1]
			});
		return pts;
	}

	internal static float ToFloat(object? v, float fallback) {
		return v switch {
			double d => (float)d,
			float f => f,
			long l => l,
			int i => i,
			_ => fallback
		};
	}

	private void RebuildSpline() {
		const int dim = 2;
		var n = m_points.Length / dim;

		if (n < 2)
			throw new InvalidOperationException($"Curve needs at least 2 control points; got {n}");

		IList<double> pts = m_points.Select(v => (double)v).ToList();

		m_spline = SplineType switch {
			SplineType.Linear =>
				Build(pts, (uint)n, dim, 1, BSpline.Type.Clamped),

			SplineType.BSpline =>
				Build(pts, (uint)n, dim, Math.Min(3u, (uint)n - 1), BSpline.Type.Clamped),

			SplineType.Bezier when (n - 1) % 3 == 0 && n >= 4 =>
				BuildBeziers(pts, n, dim),

			SplineType.Bezier =>
				Build(pts, (uint)n, dim, Math.Min(3u, (uint)n - 1), BSpline.Type.Clamped),

			SplineType.CatmullRom =>
				BSpline.InterpolateCatmullRom(pts, dim),

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

	internal static string SplineTypeToString(SplineType t) {
		return t switch {
			SplineType.Linear => "linear",
			SplineType.CatmullRom => "catmull_rom",
			SplineType.BSpline => "bspline",
			SplineType.Bezier => "bezier",
			_ => "linear"
		};
	}

	internal static SplineType ParseSplineType(string s) {
		return s switch {
			"catmull_rom" => SplineType.CatmullRom,
			"bspline" => SplineType.BSpline,
			"bezier" => SplineType.Bezier,
			_ => SplineType.Linear
		};
	}
}

public sealed class CurveFile {
	public static readonly string[] ChannelNames = ["x", "y", "z"];

	private readonly List<Curve> m_channels;

	public CurveFile(IEnumerable<Curve> channels, SplineType type, float tScale = 1f) {
		m_channels = channels.ToList();
		if (m_channels.Count is < 1 or > 3) throw new ArgumentException("A curve has 1 to 3 channels");
		SplineType = type;
		TScale = tScale;
	}

	public IReadOnlyList<Curve> Channels => m_channels;
	public CurveDimension Dimension => (CurveDimension)m_channels.Count;
	public SplineType SplineType { get; private set; }
	public float TScale { get; private set; }

	public static CurveFile FromFile(string path) {
		return FromString(File.ReadAllText(path));
	}

	public static CurveFile FromString(string toml) {
		var table = TomlSerializer.Deserialize<TomlTable>(toml)
		            ?? throw new FormatException("Curve: failed to parse TOML");

		var type = Curve.ParseSplineType(table.TryGetValue("spline_type", out var st) ? st as string ?? "" : "");
		var tScale = table.TryGetValue("t_scale", out var ts) ? Curve.ToFloat(ts, 1f) : 1f;
		var dimension = table.TryGetValue("dimension", out var d) ? d as string ?? "" : "";

		var channels = new List<Curve>();
		if (table.TryGetValue("channels", out var c) && c is TomlTableArray tables) {
			foreach (var channel in tables.Take(3)) channels.Add(new Curve(Curve.ReadPoints(channel, "points"), type, tScale));
		} else if (dimension == "3d") {
			channels.AddRange(LegacyPath(table, type, tScale));
		} else {
			var points = Curve.ReadPoints(table, "points");
			tScale *= Curve.NormalizeX(points);
			channels.Add(new Curve(points, type, tScale));
		}

		return new CurveFile(channels, type, tScale);
	}

	private static IEnumerable<Curve> LegacyPath(TomlTable table, SplineType type, float tScale) {
		var axes = new List<float>[] { [], [], [] };
		if (table.TryGetValue("points", out var p) && p is TomlTableArray pts) {
			var count = pts.Count;
			for (var i = 0; i < count; i++) {
				var t = count > 1 ? i / (float)(count - 1) : 0f;
				for (var axis = 0; axis < 3; axis++) {
					axes[axis].Add(t);
					axes[axis].Add(pts[i].TryGetValue(ChannelNames[axis], out var v) ? Curve.ToFloat(v, 0f) : 0f);
				}
			}
		}
		Log.Warn("Curve: converted an old 3D path curve to x, y and z channels");
		return axes.Select(points => new Curve(points, type, tScale));
	}

	public void Save(string path) {
		File.WriteAllText(path, Serialize());
	}

	public string Serialize() {
		var table = new TomlTable {
			["spline_type"] = Curve.SplineTypeToString(SplineType),
			["dimension"] = Dimension switch {
				CurveDimension.D2 => "2d",
				CurveDimension.D3 => "3d",
				_ => "1d"
			},
			["t_scale"] = (double)TScale
		};

		if (Dimension == CurveDimension.D1) {
			table["points"] = m_channels[0].WritePoints();
		} else {
			var channels = new TomlTableArray();
			for (var i = 0; i < m_channels.Count; i++)
				channels.Add(new TomlTable { ["name"] = ChannelNames[i], ["points"] = m_channels[i].WritePoints() });
			table["channels"] = channels;
		}

		return TomlSerializer.Serialize(table);
	}

	public void SetDimension(CurveDimension dimension) {
		var count = (int)dimension;
		while (m_channels.Count > count) m_channels.RemoveAt(m_channels.Count - 1);
		while (m_channels.Count < count) m_channels.Add(m_channels[0].Clone());
	}

	public void SetSplineType(SplineType type) {
		SplineType = type;
		foreach (var channel in m_channels) channel.SetSplineType(type);
	}

	public void SetTScale(float tScale) {
		TScale = MathF.Max(tScale, 1e-4f);
		foreach (var channel in m_channels) channel.SetTScale(TScale);
	}

	public float Eval(float t) {
		return Channel(0, t);
	}

	public (float x, float y) Eval2(float t) {
		return (Channel(0, t), Channel(1, t));
	}

	public (float x, float y, float z) Eval3(float t) {
		return (Channel(0, t), Channel(1, t), Channel(2, t));
	}

	private float Channel(int index, float t) {
		t /= TScale;
		if (Dimension == CurveDimension.D1) return m_channels[0].EvalAtX(t);
		return index < m_channels.Count ? m_channels[index].EvalAtX(t) : 0f;
	}
}
