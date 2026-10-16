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
using editor.Git;
using editor.Assets.Types;
using editor.Components.CurveCanvas;
using editor.Components.Modals;
using editor.Engine;
using editor.Workspace;

namespace editor.Editors;

public partial class CurveViewModel : Tool, IToastZoneEditor, IAutosavable {
	private const string BaseTitle = "Curve Editor";
	[ObservableProperty] private string m_bezierHint = "";
	[ObservableProperty] private string m_currentPath = "";

	[ObservableProperty] private string m_currentUid = "";

	private CurveFile? m_curve;
	private readonly List<CurveCanvasItem> m_items = [];
	[ObservableProperty] private int m_activeCurveIndex;
	[ObservableProperty] private string m_dimensionName = "1D";
	[ObservableProperty] private string m_displayTitle = BaseTitle;
	[ObservableProperty] private string m_fileName = "";
	[ObservableProperty] private bool m_isDirty;
	[ObservableProperty] private bool m_isReadOnly;
	private CurveCanvasItem? m_item;
	private bool m_loading;
	[ObservableProperty] private int m_selectedPointIndex = -1;
	[ObservableProperty] private string m_splineTypeName = "Linear";
	private bool m_syncing;
	[ObservableProperty] private float m_tScale = 1f;

	public CurveViewModel() {
		if (Design.IsDesignMode) InitDesignData();
	}

	public static IReadOnlyList<string> SplineTypeNames { get; } =
		["Linear", "Catmull-Rom", "B-Spline", "Bezier"];

	public static IReadOnlyList<string> DimensionNames { get; } = ["1D", "2D", "3D"];
	public ObservableCollection<ChannelOption> Channels { get; } = [];
	public bool HasChannels => m_curve is { Dimension: not CurveDimension.D1 };
	private static readonly string[] AxisNames = ["X", "Y", "Z"];
	private static readonly (string Key, string Fallback)[] AxisColors = [("Red", "#ff1659"), ("Green", "#3ddc84"), ("Blue", "#4d8bff")];

	public ObservableCollection<CurveCanvasItem> CanvasItems { get; } = [];
	public ObservableCollection<CurvePointVM> PointRows { get; } = [];

	public bool HasContent => m_curve is not null;
	public bool HasBezierHint => BezierHint.Length > 0;

	public bool IsAutosaveDirty => IsDirty && HasContent;

	public string? AutosaveFileName =>
		HasContent && !string.IsNullOrEmpty(CurrentPath)
			? CurrentUid + AssetTypeRegistry.GetExtension(CurrentPath)
			: null;

	public Task WriteAutosaveAsync(string virtualPath) {
		if (m_curve is not { } curve) return Task.CompletedTask;
		var realPath = ProjectContext.Resolve(virtualPath);
		return Task.Run(() => curve.Save(realPath));
	}

	public void OpenFile(string uid, string virtualPath, BaseAsset definition, string? contentSourceRealPath = null) {
		m_loading = true;
		var recovered = contentSourceRealPath != null;
		try {
			var curve = CurveFile.FromFile(contentSourceRealPath ?? ProjectContext.Resolve(virtualPath));
			CurrentUid = uid;
			CurrentPath = virtualPath;
			LoadCurve(curve, virtualPath);
		} catch (Exception e) {
			Log.Error($"Curve Editor: failed to open '{virtualPath}': {e.Message}");
			CloseCurrent();
			recovered = false;
		} finally {
			IsDirty = recovered;
			m_loading = false;
		}
	}

	public async Task<bool> ConfirmCloseCurrentAsync() {
		if (!IsDirty && HasContent) AutosaveService.Delete(CurrentUid, AssetTypeRegistry.GetExtension(CurrentPath));
		if (!IsDirty || !HasContent) return true;
		if (App.MainWindow is not { } owner) return true;
		var result = await new MessageModal(new ModalConfig(
			"Unsaved Changes",
			$"Save changes to '{FileName}'?",
			ModalButtons.OkNoCancel,
			OkLabel: "Save"
		)).ShowDialog<bool?>(owner);
		if (result is null) return false;
		if (result is true) {
			await Save();
			return !IsDirty;
		}

		AutosaveService.Delete(CurrentUid, AssetTypeRegistry.GetExtension(CurrentPath));
		return true;
	}

	private void InitDesignData() {
		var curve = new CurveFile(
			[new Curve([0f, 0f, 0.3f, 0.9f, 0.65f, 0.35f, 1f, 1f], SplineType.CatmullRom)], SplineType.CatmullRom);
		LoadCurve(curve, "sample.tcurve");
	}

	private void LoadCurve(CurveFile curve, string virtualPath) {
		m_loading = true;

		m_curve = curve;
		FileName = Path.GetFileName(virtualPath);
		IsReadOnly = false;

		RebuildItems();
		DimensionName = DimensionNames[(int)curve.Dimension - 1];
		SplineTypeName = ToName(curve.SplineType);
		TScale = curve.TScale;
		UpdateTitle();
		OnPropertyChanged(nameof(HasContent));

		m_loading = false;
	}

	private void RebuildItems() {
		foreach (var item in m_items) item.Changed -= OnCurveChanged;
		m_items.Clear();
		CanvasItems.Clear();
		if (m_curve is null) return;

		for (var i = 0; i < m_curve.Channels.Count; i++) {
			var (key, fallback) = AxisColors[i];
			var item = new CurveCanvasItem(m_curve.Channels[i], CurveCanvas.ResolveColor(key, fallback),
				m_curve.Dimension == CurveDimension.D1 ? "" : AxisNames[i]);
			item.Changed += OnCurveChanged;
			m_items.Add(item);
			CanvasItems.Add(item);
		}

		Channels.Clear();
		for (var i = 0; i < m_items.Count; i++)
			Channels.Add(new ChannelOption(i, AxisNames[i], m_items.Count));
		OnPropertyChanged(nameof(HasChannels));
		SelectChannel(Math.Clamp(ActiveCurveIndex, 0, m_items.Count - 1));
	}

	[RelayCommand]
	private void PickChannel(ChannelOption? channel) {
		if (channel is null || channel.Index >= m_items.Count) return;
		// disable toggling on the button lol
		if (channel.Index == ActiveCurveIndex) {
			channel.IsActive = false;
			channel.IsActive = true;
			return;
		}

		SelectChannel(channel.Index);
	}

	private void SelectChannel(int index) {
		ActiveCurveIndex = index;
		foreach (var channel in Channels) channel.IsActive = channel.Index == index;
		m_item = m_items.Count > index ? m_items[index] : null;
		SelectedPointIndex = -1;
		PointRows.Clear();
		SyncRows();
		UpdateBezierHint();
	}

	partial void OnDimensionNameChanged(string value) {
		if (m_loading || m_curve is null) return;
		var dimension = value switch { "2D" => CurveDimension.D2, "3D" => CurveDimension.D3, _ => CurveDimension.D1 };
		if (dimension == m_curve.Dimension) return;
		_ = ChangeDimensionAsync(dimension);
	}

	private async Task ChangeDimensionAsync(CurveDimension dimension) {
		if (m_curve is null) return;
		if (dimension < m_curve.Dimension && App.MainWindow is { } owner) {
			var dropped = AxisNames[(int)dimension..m_curve.Channels.Count];
			var confirmed = await new MessageModal(new ModalConfig(
				"Change Dimension",
				$"Going to {DimensionName} deletes the {string.Join(" and ", dropped)} {(dropped.Length > 1 ? "channels" : "channel")}. Continue?",
				ModalButtons.OkCancel,
				OkLabel: "Delete"
			)).ShowDialog<bool?>(owner);
			if (confirmed is not true) {
				m_loading = true;
				DimensionName = DimensionNames[(int)m_curve.Dimension - 1];
				m_loading = false;
				return;
			}
		}

		m_curve.SetDimension(dimension);
		RebuildItems();
		IsDirty = true;
	}

	private void CloseCurrent() {
		foreach (var item in m_items) item.Changed -= OnCurveChanged;
		m_items.Clear();
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

	[RelayCommand]
	private async Task Save() {
		if (m_curve is null || string.IsNullOrEmpty(CurrentPath)) return;
		var curve = m_curve;
		if (await GitLockGuard.SaveAsync(CurrentUid, CurrentPath, path => Task.Run(() => curve.Save(path)), ReopenFile))
			IsDirty = false;
	}

	private void ReopenFile(string uid, string virtualPath) {
		OpenFile(uid, virtualPath, GitLockGuard.DefinitionOf(virtualPath)!);
	}

	[RelayCommand]
	private void AddPoint() {
		if (m_item is null || IsReadOnly) return;

		// Splits the widest gap between keys
		var n = m_item.NumPoints;
		var step = m_item.Curve.SplineType == SplineType.Bezier && (n - 1) % 3 == 0 ? 3 : 1;
		var widest = 0;
		var gap = -1f;
		for (var i = 0; i + step < n; i += step) {
			var g = m_item.GetPoint(i + step).X - m_item.GetPoint(i).X;
			if (g <= gap) continue;
			gap = g;
			widest = i;
		}

		var (ax, ay) = m_item.GetPoint(widest);
		var (bx, by) = m_item.GetPoint(widest + step);
		SelectedPointIndex = m_item.InsertPoint((ax + bx) / 2f, (ay + by) / 2f);
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
		var n = m_item.NumPoints;

		if (PointRows.Count != n) {
			PointRows.Clear();
			for (var i = 0; i < n; i++) {
				var (x, y) = m_item.GetPoint(i);
				PointRows.Add(new CurvePointVM(CommitPoint, i, x, y, m_item.IsHandle(i)) {
					IsSelected = i == SelectedPointIndex
				});
			}
		} else {
			for (var i = 0; i < n; i++) {
				var (x, y) = m_item.GetPoint(i);
				PointRows[i].X = x;
				PointRows[i].Y = y;
			}
		}

		m_syncing = false;
	}

	private void UpdateBezierHint() {
		var hint = "";
		if (m_item?.Curve is { SplineType: SplineType.Bezier } c && (c.NumPoints - 1) % 3 != 0)
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

	partial void OnIsDirtyChanged(bool value) {
		UpdateTitle();
	}

	partial void OnSelectedPointIndexChanged(int value) {
		for (var i = 0; i < PointRows.Count; i++)
			PointRows[i].IsSelected = i == value;
	}

	partial void OnSplineTypeNameChanged(string value) {
		if (m_loading || m_syncing || m_curve is null || IsReadOnly) return;
		var type = FromName(value);
		if (type == m_curve.SplineType) return;
		m_curve.SetSplineType(type);
		foreach (var item in m_items) item.NotifyStructureChanged();
	}

	partial void OnTScaleChanged(float value) {
		if (m_loading || m_syncing || m_curve is null || IsReadOnly) return;
		if (Math.Abs(value - m_curve.TScale) < 1e-9f) return;
		m_curve.SetTScale(value);
		IsDirty = true;
	}

	private static string ToName(SplineType t) {
		return t switch {
			SplineType.CatmullRom => "Catmull-Rom",
			SplineType.BSpline => "B-Spline",
			SplineType.Bezier => "Bezier",
			_ => "Linear"
		};
	}

	private static SplineType FromName(string s) {
		return s switch {
			"Catmull-Rom" => SplineType.CatmullRom,
			"B-Spline" => SplineType.BSpline,
			"Bezier" => SplineType.Bezier,
			_ => SplineType.Linear
		};
	}
}

// One row in an editor's left-panel point list, kept in sync with the canvas
public sealed partial class CurvePointVM : ObservableObject {
	private readonly Action<int, float, float> m_commit;
	[ObservableProperty] private bool m_isSelected;

	[ObservableProperty] private float m_x;
	[ObservableProperty] private float m_y;

	public CurvePointVM(Action<int, float, float> commit, int index, float x, float y, bool isHandle, bool lockX = false) {
		m_commit = commit;
		Index = index;
		Label = index.ToString();
		IsHandle = isHandle;
		LockX = lockX;
		m_x = x;
		m_y = y;
	}

	public int Index { get; }
	public string Label { get; }
	public bool IsHandle { get; }
	public bool LockX { get; }
	public bool CanEditX => !LockX;

	partial void OnXChanged(float value) {
		m_commit(Index, value, Y);
	}

	partial void OnYChanged(float value) {
		m_commit(Index, X, value);
	}
}

public sealed partial class ChannelOption : ObservableObject {
	[ObservableProperty] private bool m_isActive;

	public ChannelOption(int index, string name, int count) {
		Index = index;
		Name = name;
		Corners = new Avalonia.CornerRadius(index == 0 ? 6 : 0, index == count - 1 ? 6 : 0, index == count - 1 ? 6 : 0, index == 0 ? 6 : 0);
	}

	public int Index { get; }
	public string Name { get; }
	public Avalonia.CornerRadius Corners { get; }
	public bool IsX => Index == 0;
	public bool IsY => Index == 1;
	public bool IsZ => Index == 2;
}
