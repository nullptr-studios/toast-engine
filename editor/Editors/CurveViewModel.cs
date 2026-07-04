//
// CurveViewModel.cs by Xein
// 04 Jul 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.CurveCanvas;
using editor.Components.Modals;
using editor.Engine;

namespace editor.Editors;

public partial class CurveViewModel : Tool, IToastZoneEditor {
	private const string BaseTitle = "Curve Editor";

	[ObservableProperty] private string m_currentUid = "";
	[ObservableProperty] private string m_currentPath = "";
	[ObservableProperty] private string m_fileName = "";
	[ObservableProperty] private string m_displayTitle = BaseTitle;
	[ObservableProperty] private bool m_isDirty;
	[ObservableProperty] private bool m_isReadOnly;
	[ObservableProperty] private string m_splineTypeName = "Linear";
	[ObservableProperty] private float m_tScale = 1f;
	[ObservableProperty] private int m_selectedPointIndex = -1;
	[ObservableProperty] private string m_bezierHint = "";

	public static IReadOnlyList<string> SplineTypeNames { get; } =
		["Linear", "Catmull-Rom", "B-Spline", "Bezier"];

	public ObservableCollection<CurveCanvasItem> CanvasItems { get; } = [];
	public ObservableCollection<CurvePointVM> PointRows { get; } = [];

	public bool HasContent => m_curve is not null;
	public bool HasBezierHint => BezierHint.Length > 0;

	private Curve? m_curve;
	private CurveCanvasItem? m_item;
	private bool m_loading;
	private bool m_syncing;

	public CurveViewModel() {
		if (Design.IsDesignMode) InitDesignData();
	}

	private void InitDesignData() {
		var curve = new Curve(
			[0f, 0f, 0.3f, 0.9f, 0.65f, 0.35f, 1f, 1f],
			CurveDimension.D2, SplineType.CatmullRom);
		LoadCurve(curve, "sample.tcurve");
	}

	public void OpenFile(string uid, string virtualPath, BaseAsset definition) {
		m_loading = true;
		try {
			var curve = Curve.FromFile(ProjectContext.Resolve(virtualPath));
			CurrentUid = uid;
			CurrentPath = virtualPath;
			LoadCurve(curve, virtualPath);
		} catch (Exception e) {
			Log.Error($"Curve Editor: failed to open '{virtualPath}': {e.Message}");
			CloseCurrent();
		} finally {
			IsDirty = false;
			m_loading = false;
		}
	}

	private void LoadCurve(Curve curve, string virtualPath) {
		m_loading = true;

		if (m_item is not null) m_item.Changed -= OnCurveChanged;

		m_curve = curve;
		FileName = Path.GetFileName(virtualPath);
		IsReadOnly = curve.Dimension == CurveDimension.D3;

		m_item = new CurveCanvasItem(curve, CurveCanvas.ResolveColor("Red", "#ff1659")) {
			IsEditable = !IsReadOnly,
		};
		m_item.Changed += OnCurveChanged;

		CanvasItems.Clear();
		CanvasItems.Add(m_item);

		SplineTypeName = ToName(curve.SplineType);
		TScale = curve.TScale;
		SelectedPointIndex = -1;
		SyncRows();
		UpdateBezierHint();
		UpdateTitle();
		OnPropertyChanged(nameof(HasContent));

		m_loading = false;
	}

	private void CloseCurrent() {
		if (m_item is not null) m_item.Changed -= OnCurveChanged;
		m_item = null;
		m_curve = null;
		CurrentUid = "";
		CurrentPath = "";
		FileName = "";
		CanvasItems.Clear();
		PointRows.Clear();
		SelectedPointIndex = -1;
		BezierHint = "";
		OnPropertyChanged(nameof(HasBezierHint));
		UpdateTitle();
		OnPropertyChanged(nameof(HasContent));
	}

	public async Task<bool> ConfirmCloseCurrentAsync() {
		if (!IsDirty || !HasContent) return true;
		if (App.MainWindow is not { } owner) return true;
		var result = await new MessageModal(new ModalConfig(
			Title: "Unsaved Changes",
			Message: $"Save changes to '{FileName}'?",
			Buttons: ModalButtons.OkNoCancel,
			OkLabel: "Save"
		)).ShowDialog<bool?>(owner);
		if (result is null) return false;
		if (result is true) await Save();
		return true;
	}

	[RelayCommand]
	private async Task Save() {
		if (m_curve is null || string.IsNullOrEmpty(CurrentPath)) return;
		var realPath = ProjectContext.Resolve(CurrentPath);
		await Task.Run(() => m_curve.Save(realPath));
		IsDirty = false;
	}

	[RelayCommand]
	private void AddPoint() {
		if (m_item is null || IsReadOnly) return;

		int n = m_item.NumPoints;
		var (lastX, lastY) = m_item.GetPoint(n - 1);
		var (firstX, _) = m_item.GetPoint(0);
		float dx = n > 1 ? Math.Max((lastX - firstX) / (n - 1), 1e-3f) : 0.25f;

		SelectedPointIndex = m_item.InsertPoint(lastX + dx, lastY);
	}

	[RelayCommand]
	private void RemovePoint(CurvePointVM? row) {
		if (m_item is null || row is null || IsReadOnly) return;
		m_item.RemovePoint(row.Index);
		SelectedPointIndex = Math.Min(SelectedPointIndex, m_item.NumPoints - 1);
	}

	internal void CommitPoint(int index, float x, float y) {
		if (m_loading || m_syncing || m_item is null || IsReadOnly) return;
		m_item.MovePoint(index, x, y);
	}

	private void OnCurveChanged() {
		if (m_loading) return;
		SyncRows();
		UpdateBezierHint();
		IsDirty = true;
	}

	private void SyncRows() {
		if (m_item is null) {
			PointRows.Clear();
			return;
		}

		m_syncing = true;
		int n = m_item.NumPoints;

		if (PointRows.Count != n) {
			PointRows.Clear();
			for (int i = 0; i < n; i++) {
				var (x, y) = m_item.GetPoint(i);
				PointRows.Add(new CurvePointVM(CommitPoint, i, x, y, m_item.IsHandle(i)) {
					IsSelected = i == SelectedPointIndex,
				});
			}
		} else {
			for (int i = 0; i < n; i++) {
				var (x, y) = m_item.GetPoint(i);
				PointRows[i].X = x;
				PointRows[i].Y = y;
			}
		}

		m_syncing = false;
	}

	private void UpdateBezierHint() {
		string hint = "";
		if (m_curve is { SplineType: SplineType.Bezier } c && (c.NumPoints - 1) % 3 != 0)
			hint = $"Bezier needs 3k+1 points (has {c.NumPoints}) — rendering as B-Spline";
		BezierHint = hint;
		OnPropertyChanged(nameof(HasBezierHint));
	}

	private void UpdateTitle() {
		Title = IsDirty ? BaseTitle + " *" : BaseTitle;
		DisplayTitle = string.IsNullOrEmpty(FileName) ? BaseTitle
			: IsDirty ? $"{FileName} *"
			: FileName;
	}

	partial void OnIsDirtyChanged(bool value) => UpdateTitle();

	partial void OnSelectedPointIndexChanged(int value) {
		for (int i = 0; i < PointRows.Count; i++)
			PointRows[i].IsSelected = i == value;
	}

	partial void OnSplineTypeNameChanged(string value) {
		if (m_loading || m_syncing || m_curve is null || IsReadOnly) return;
		var type = FromName(value);
		if (type == m_curve.SplineType) return;
		m_curve.SetSplineType(type);
		m_item?.NotifyStructureChanged();
	}

	partial void OnTScaleChanged(float value) {
		if (m_loading || m_syncing || m_curve is null || IsReadOnly) return;
		if (Math.Abs(value - m_curve.TScale) < 1e-9f) return;
		m_curve.SetTScale(value);
		IsDirty = true;
	}

	private static string ToName(SplineType t) => t switch {
		SplineType.CatmullRom => "Catmull-Rom",
		SplineType.BSpline => "B-Spline",
		SplineType.Bezier => "Bezier",
		_ => "Linear"
	};

	private static SplineType FromName(string s) => s switch {
		"Catmull-Rom" => SplineType.CatmullRom,
		"B-Spline" => SplineType.BSpline,
		"Bezier" => SplineType.Bezier,
		_ => SplineType.Linear
	};
}

// One row in an editor's left-panel point list, kept in sync with the canvas
public sealed partial class CurvePointVM : ObservableObject {
	private readonly Action<int, float, float> m_commit;

	public int Index { get; }
	public string Label { get; }
	public bool IsHandle { get; }

	[ObservableProperty] private float m_x;
	[ObservableProperty] private float m_y;
	[ObservableProperty] private bool m_isSelected;

	public CurvePointVM(Action<int, float, float> commit, int index, float x, float y, bool isHandle) {
		m_commit = commit;
		Index = index;
		Label = index.ToString();
		IsHandle = isHandle;
		m_x = x;
		m_y = y;
	}

	partial void OnXChanged(float value) => m_commit(Index, value, Y);
	partial void OnYChanged(float value) => m_commit(Index, X, value);
}
