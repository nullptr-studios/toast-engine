//
// HapticsViewModel.cs by Xein
// 04 Jul 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Assets.Types;
using editor.Components.CurveCanvas;
using editor.Components.Modals;
using editor.Engine;

namespace editor.Editors;

public partial class HapticsViewModel : Tool, IToastZoneEditor {
	private const string BaseTitle = "Haptics Editor";
	private readonly List<CurveCanvasItem> m_editItems = [];
	[ObservableProperty] private int m_activeCurveIndex;
	[ObservableProperty] private string m_bezierHint = "";
	[ObservableProperty] private string m_channelsName = "Single";
	[ObservableProperty] private string m_currentPath = "";

	[ObservableProperty] private string m_currentUid = "";
	[ObservableProperty] private string m_curveSplineTypeName = "Catmull-Rom";
	[ObservableProperty] private string m_displayTitle = BaseTitle;
	[ObservableProperty] private int m_durationMs = 200;
	[ObservableProperty] private string m_editTargetName = "Left";
	[ObservableProperty] private string m_fileName = "";

	private Haptic? m_haptic;
	[ObservableProperty] private bool m_isDirty;
	[ObservableProperty] private float m_left = 0.5f;
	private bool m_loading;
	[ObservableProperty] private string m_modeName = "Standard";
	[ObservableProperty] private float m_multiplier = 1f;
	[ObservableProperty] private float m_pan;
	[ObservableProperty] private int m_priority;
	[ObservableProperty] private float m_right = 0.5f;
	[ObservableProperty] private int m_selectedPointIndex = -1;
	private bool m_syncing;
	[ObservableProperty] private bool m_unsupportedMode;

	public HapticsViewModel() {
		if (Design.IsDesignMode) InitDesignData();
	}

	public static IReadOnlyList<string> ModeNames { get; } = ["Standard", "Curve"];
	public static IReadOnlyList<string> ChannelNames { get; } = ["Single", "Dual"];
	public static IReadOnlyList<string> EditTargetNames { get; } = ["Left", "Right"];

	public ObservableCollection<CurveCanvasItem> CanvasItems { get; } = [];
	public ObservableCollection<CurvePointVM> PointRows { get; } = [];

	public bool HasContent => m_haptic is not null;
	public bool HasBezierHint => BezierHint.Length > 0;
	public bool IsStandardMode => m_haptic?.Mode == HapticMode.Standard;
	public bool IsCurveMode => m_haptic?.Mode == HapticMode.Curve;
	public bool IsDualChannel => IsCurveMode && m_haptic?.Channels == HapticChannels.Dual;
	public bool IsSingleChannel => IsCurveMode && m_haptic?.Channels == HapticChannels.Single;
	public bool CanvasReadOnly => !IsCurveMode || UnsupportedMode;
	public bool InputsEnabled => !UnsupportedMode;

	private CurveCanvasItem? ActiveEditItem =>
		ActiveCurveIndex >= 0 && ActiveCurveIndex < m_editItems.Count
			? m_editItems[ActiveCurveIndex]
			: null;

	private bool Ignore => m_loading || m_syncing || m_haptic is null || UnsupportedMode;

	public void OpenFile(string uid, string virtualPath, BaseAsset definition) {
		m_loading = true;
		try {
			var haptic = Haptic.FromFile(ProjectContext.Resolve(virtualPath));
			CurrentUid = uid;
			CurrentPath = virtualPath;
			LoadHaptic(haptic, virtualPath);
		} catch (Exception e) {
			Log.Error($"Haptics Editor: failed to open '{virtualPath}': {e.Message}");
			CloseCurrent();
		} finally {
			IsDirty = false;
			m_loading = false;
		}
	}

	public async Task<bool> ConfirmCloseCurrentAsync() {
		if (!IsDirty || !HasContent) return true;
		if (App.MainWindow is not { } owner) return true;
		var result = await new MessageModal(new ModalConfig(
			"Unsaved Changes",
			$"Save changes to '{FileName}'?",
			ModalButtons.OkNoCancel,
			OkLabel: "Save"
		)).ShowDialog<bool?>(owner);
		if (result is null) return false;
		if (result is true) await Save();
		return true;
	}

	private void InitDesignData() {
		var haptic = new Haptic {
			Mode = HapticMode.Curve,
			DurationMs = 350,
			Curve = new Curve([0f, 0f, 0.35f, 1f, 1f, 0f], CurveDimension.D2, SplineType.CatmullRom)
		};
		LoadHaptic(haptic, "sample.thaptic");
	}

	private void LoadHaptic(Haptic haptic, string virtualPath) {
		m_loading = true;

		m_haptic = haptic;
		FileName = Path.GetFileName(virtualPath);
		UnsupportedMode = haptic.Mode is HapticMode.AdaptiveTrigger or HapticMode.AudioHaptic;

		ModeName = haptic.Mode switch {
			HapticMode.Curve => "Curve",
			HapticMode.AdaptiveTrigger => "Adaptive Trigger",
			HapticMode.AudioHaptic => "Audio Haptic",
			_ => "Standard"
		};
		Priority = haptic.Priority;
		DurationMs = haptic.DurationMs;
		Left = haptic.Left;
		Right = haptic.Right;
		ChannelsName = haptic.Channels == HapticChannels.Dual ? "Dual" : "Single";
		Pan = haptic.Pan;
		Multiplier = haptic.Multiplier;
		EditTargetName = "Left";
		SelectedPointIndex = -1;

		RebuildCanvas();
		UpdateTitle();
		NotifyModeFlags();

		m_loading = false;
	}

	private void CloseCurrent() {
		m_haptic = null;
		CurrentUid = "";
		CurrentPath = "";
		FileName = "";
		UnhookEditItems();
		CanvasItems.Clear();
		PointRows.Clear();
		SelectedPointIndex = -1;
		BezierHint = "";
		OnPropertyChanged(nameof(HasBezierHint));
		UpdateTitle();
		NotifyModeFlags();
	}

	[RelayCommand]
	private async Task Save() {
		if (m_haptic is null || string.IsNullOrEmpty(CurrentPath)) return;
		var haptic = m_haptic;
		var realPath = ProjectContext.Resolve(CurrentPath);
		await Task.Run(() => haptic.Save(realPath));
		IsDirty = false;
	}

	[RelayCommand]
	private void TestHaptic() {
		if (m_haptic is null || !ToastEngine.IsEngineReady) return;
		// serializes the unsaved state so tweaks are testable immediately
		ToastEngine.TestHaptic(m_haptic.Serialize());
	}

	private void RebuildCanvas() {
		UnhookEditItems();
		CanvasItems.Clear();

		if (m_haptic is null) return;

		if (m_haptic.Mode == HapticMode.Curve) {
			var dual = m_haptic.Channels == HapticChannels.Dual;

			var leftCurve = m_haptic.Curve;
			if (leftCurve is null) return;

			if (dual) {
				var rightCurve = m_haptic.CurveRight ??= leftCurve.Clone();
				AddEditItem(leftCurve, CurveCanvas.ResolveColor("Green", "#2fbf71"), "Left");
				AddEditItem(rightCurve, CurveCanvas.ResolveColor("Blue", "#3d8bfd"), "Right");
			} else {
				AddEditItem(leftCurve, CurveCanvas.ResolveColor("Red", "#ff1659"), "Intensity");
			}

			ActiveCurveIndex = IsDualChannel && EditTargetName == "Right" ? 1 : 0;
			CurveSplineTypeName = ToName((ActiveEditItem ?? m_editItems[0]).Curve.SplineType);
			SelectedPointIndex = -1;
			SyncRows();
			UpdateBezierHint();
			return;
		}

		if (m_haptic.Mode == HapticMode.Standard) {
			// live preview
			CanvasItems.Add(MakePreviewItem(m_haptic.Left, CurveCanvas.ResolveColor("Green", "#2fbf71"), "Left"));
			CanvasItems.Add(MakePreviewItem(m_haptic.Right, CurveCanvas.ResolveColor("Blue", "#3d8bfd"), "Right"));
			PointRows.Clear();
			BezierHint = "";
			OnPropertyChanged(nameof(HasBezierHint));
		}
		// unsupported modes: empty canvas
	}

	private void AddEditItem(Curve curve, Color color, string label) {
		var item = new CurveCanvasItem(curve, color, label);
		item.Changed += OnCurveChanged;
		m_editItems.Add(item);
		CanvasItems.Add(item);
	}

	private void UnhookEditItems() {
		foreach (var item in m_editItems) item.Changed -= OnCurveChanged;
		m_editItems.Clear();
	}

	private static CurveCanvasItem MakePreviewItem(float intensity, Color color, string label) {
		return new CurveCanvasItem(new Curve([0f, intensity, 1f, intensity], CurveDimension.D2, SplineType.Linear), color,
			label) {
			IsEditable = false
		};
	}

	private void RefreshPreview() {
		if (m_haptic is null || m_haptic.Mode != HapticMode.Standard) return;
		if (CanvasItems.Count != 2) {
			RebuildCanvas();
			return;
		}

		CanvasItems[0].Curve.SetPoints([0f, m_haptic.Left, 1f, m_haptic.Left]);
		CanvasItems[0].NotifyStructureChanged();
		CanvasItems[1].Curve.SetPoints([0f, m_haptic.Right, 1f, m_haptic.Right]);
		CanvasItems[1].NotifyStructureChanged();
	}

	internal void CommitPoint(int index, float x, float y) {
		if (m_loading || m_syncing || ActiveEditItem is not { } item) return;
		item.MovePoint(index, Math.Clamp(x, 0f, 1f), Math.Clamp(y, 0f, 1f));
	}

	private void OnCurveChanged() {
		if (m_loading) return;
		SyncRows();
		UpdateBezierHint();
		IsDirty = true;
	}

	private void SyncRows() {
		if (ActiveEditItem is not { } item) {
			PointRows.Clear();
			return;
		}

		m_syncing = true;
		var n = item.NumPoints;

		if (PointRows.Count != n) {
			PointRows.Clear();
			for (var i = 0; i < n; i++) {
				var (x, y) = item.GetPoint(i);
				PointRows.Add(new CurvePointVM(CommitPoint, i, x, y, item.IsHandle(i)) {
					IsSelected = i == SelectedPointIndex
				});
			}
		} else {
			for (var i = 0; i < n; i++) {
				var (x, y) = item.GetPoint(i);
				PointRows[i].X = x;
				PointRows[i].Y = y;
			}
		}

		m_syncing = false;
	}

	[RelayCommand]
	private void AddPoint() {
		if (ActiveEditItem is not { } item) return;

		var n = item.NumPoints;
		var (lastX, lastY) = item.GetPoint(n - 1);
		var x = Math.Clamp(lastX + 0.1f, 0f, 1f);
		SelectedPointIndex = item.InsertPoint(x, lastY);
	}

	[RelayCommand]
	private void RemovePoint(CurvePointVM? row) {
		if (ActiveEditItem is not { } item || row is null) return;
		item.RemovePoint(row.Index);
		SelectedPointIndex = Math.Min(SelectedPointIndex, item.NumPoints - 1);
	}

	private void UpdateBezierHint() {
		var hint = "";
		if (ActiveEditItem?.Curve is { SplineType: SplineType.Bezier } c && (c.NumPoints - 1) % 3 != 0)
			hint = $"Bezier needs 3k+1 points (has {c.NumPoints}) — rendering as B-Spline";
		BezierHint = hint;
		OnPropertyChanged(nameof(HasBezierHint));
	}

	partial void OnModeNameChanged(string value) {
		if (Ignore) return;
		var mode = value == "Curve" ? HapticMode.Curve : HapticMode.Standard;
		if (mode == m_haptic!.Mode) return;

		m_haptic.Mode = mode;
		// first switch into curve mode: start with a classic pulse envelope
		if (mode == HapticMode.Curve)
			m_haptic.Curve ??= new Curve([0f, 0f, 0.5f, 1f, 1f, 0f], CurveDimension.D2, SplineType.CatmullRom);

		RebuildCanvas();
		NotifyModeFlags();
		IsDirty = true;
	}

	partial void OnChannelsNameChanged(string value) {
		if (Ignore) return;
		var channels = value == "Dual" ? HapticChannels.Dual : HapticChannels.Single;
		if (channels == m_haptic!.Channels) return;

		m_haptic.Channels = channels;
		EditTargetName = "Left";
		RebuildCanvas();
		NotifyModeFlags();
		IsDirty = true;
	}

	partial void OnEditTargetNameChanged(string value) {
		if (m_loading || m_syncing) return;
		ActiveCurveIndex = value == "Right" && m_editItems.Count > 1 ? 1 : 0;
		SelectedPointIndex = -1;
		if (ActiveEditItem is { } item) CurveSplineTypeName = ToName(item.Curve.SplineType);
		SyncRows();
		UpdateBezierHint();
	}

	partial void OnCurveSplineTypeNameChanged(string value) {
		if (Ignore || ActiveEditItem is not { } item) return;
		var type = FromName(value);
		if (type == item.Curve.SplineType) return;
		item.Curve.SetSplineType(type);
		item.NotifyStructureChanged();
	}

	partial void OnPriorityChanged(int value) {
		if (Ignore) return;
		m_haptic!.Priority = value;
		IsDirty = true;
	}

	partial void OnDurationMsChanged(int value) {
		if (Ignore) return;
		m_haptic!.DurationMs = Math.Max(value, 0);
		IsDirty = true;
	}

	partial void OnLeftChanged(float value) {
		if (Ignore) return;
		m_haptic!.Left = Math.Clamp(value, 0f, 1f);
		RefreshPreview();
		IsDirty = true;
	}

	partial void OnRightChanged(float value) {
		if (Ignore) return;
		m_haptic!.Right = Math.Clamp(value, 0f, 1f);
		RefreshPreview();
		IsDirty = true;
	}

	partial void OnPanChanged(float value) {
		if (Ignore) return;
		m_haptic!.Pan = Math.Clamp(value, -1f, 1f);
		IsDirty = true;
	}

	partial void OnMultiplierChanged(float value) {
		if (Ignore) return;
		m_haptic!.Multiplier = Math.Max(value, 0f);
		IsDirty = true;
	}

	partial void OnIsDirtyChanged(bool value) {
		UpdateTitle();
	}

	partial void OnSelectedPointIndexChanged(int value) {
		for (var i = 0; i < PointRows.Count; i++)
			PointRows[i].IsSelected = i == value;
	}

	private void NotifyModeFlags() {
		OnPropertyChanged(nameof(HasContent));
		OnPropertyChanged(nameof(IsStandardMode));
		OnPropertyChanged(nameof(IsCurveMode));
		OnPropertyChanged(nameof(IsDualChannel));
		OnPropertyChanged(nameof(IsSingleChannel));
		OnPropertyChanged(nameof(CanvasReadOnly));
		OnPropertyChanged(nameof(InputsEnabled));
	}

	partial void OnUnsupportedModeChanged(bool value) {
		OnPropertyChanged(nameof(CanvasReadOnly));
		OnPropertyChanged(nameof(InputsEnabled));
	}

	private void UpdateTitle() {
		Title = IsDirty ? BaseTitle + " *" : BaseTitle;
		DisplayTitle = string.IsNullOrEmpty(FileName) ? BaseTitle
			: IsDirty ? $"{FileName} *"
			: FileName;
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
