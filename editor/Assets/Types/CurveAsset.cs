using System.Threading.Tasks;
using Lucide.Avalonia;

namespace editor.Assets.Types;

public sealed class CurveAsset : BaseAsset {
	public override string Type => "curve";
	public override string Extension => ".tcurve";
	public override string DisplayName => "Curve";
	public override string ChipText => "CURVE";
	public override string ChipColor => "Cyan";
	public override LucideIconKind Icon => LucideIconKind.Spline;
	public override bool CanBeCreated => true;
	public override string Category => "Data";
	public override bool HasThumbnail => false;
	public override bool CanBeEdited => true;
	public override string EditorTool => "CurveEditor";
	public override string SchemaPath => "core://schemas/curve.schema.json";

	public override Task CreateAsync(string path) {
		new Curve([0f, 0f, 1f, 1f], CurveDimension.D2, SplineType.Linear).Save(path);
		return Task.CompletedTask;
	}
}
