using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace editor.Editors;

public partial class PaletteView : UserControl {
	private const double DragThreshold = 4;
	private const double CollapsedWidth = 28;
	private const int Columns = 16;

	private static readonly int[] s_regionColumns = [0, 4, 6];
	private const int AnimationMs = 180;
	private readonly GridLength[] m_savedWidths = new GridLength[7];
	private readonly double[] m_savedPixels = new double[7];
	private readonly bool[] m_collapsed = new bool[7];
	private readonly DispatcherTimer?[] m_timers = new DispatcherTimer?[7];

	private enum PressMode { None, Select, Move, Copy }

	private PressMode m_mode;
	private bool m_dragging;
	private Point m_pressPoint;
	private int m_pressCell;
	private int m_dropOffset;
	private PaletteViewModel? m_vm;

	public PaletteView() {
		InitializeComponent();
		SwatchGrid.AddHandler(PointerPressedEvent, OnSwatchPressed, RoutingStrategies.Tunnel);
		SwatchGrid.PointerMoved += OnSwatchMoved;
		SwatchGrid.PointerReleased += OnSwatchReleased;
		SwatchGrid.PointerCaptureLost += (_, _) => EndPress(false);
	}

	private PaletteViewModel? Vm => DataContext as PaletteViewModel;

	protected override void OnDataContextChanged(EventArgs e) {
		base.OnDataContextChanged(e);
		if (m_vm is not null) m_vm.PropertyChanged -= OnVmPropertyChanged;
		m_vm = Vm;
		if (m_vm is not null) m_vm.PropertyChanged += OnVmPropertyChanged;
		ApplyCollapsed();
	}

	private void OnHeaderTapped(object? sender, TappedEventArgs e) {
		if (sender is not Control { Tag: string tag } || Vm is not { } vm) return;
		switch (tag) {
			case "labels": vm.IsLabelsCollapsed = !vm.IsLabelsCollapsed; break;
			case "entry": vm.IsEntryCollapsed = !vm.IsEntryCollapsed; break;
			case "physics": vm.IsPhysicsCollapsed = !vm.IsPhysicsCollapsed; break;
		}
	}

	private void OnVmPropertyChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName is nameof(PaletteViewModel.IsLabelsCollapsed) or nameof(PaletteViewModel.IsEntryCollapsed)
		    or nameof(PaletteViewModel.IsPhysicsCollapsed))
			ApplyCollapsed();
	}

	private void ApplyCollapsed() {
		if (Vm is not { } vm) return;
		SetCollapsed(0, vm.IsLabelsCollapsed);
		SetCollapsed(4, vm.IsEntryCollapsed);
		SetCollapsed(6, vm.IsPhysicsCollapsed);
	}

	private void SetCollapsed(int column, bool collapsed) {
		if (m_collapsed[column] == collapsed) return;
		m_collapsed[column] = collapsed;

		var definition = Root.ColumnDefinitions[column];
		var current = definition.ActualWidth;
		if (collapsed) {
			m_savedWidths[column] = definition.Width;
			m_savedPixels[column] = current;
			definition.MinWidth = 0;
			AnimateWidth(column, current, CollapsedWidth, new GridLength(CollapsedWidth));
		} else {
			var restored = m_savedWidths[column].Value > 0 ? m_savedWidths[column] : new GridLength(1, GridUnitType.Star);
			AnimateWidth(column, current, Math.Max(m_savedPixels[column], CollapsedWidth), restored);
		}
	}

	private void AnimateWidth(int column, double from, double to, GridLength final) {
		m_timers[column]?.Stop();

		var definition = Root.ColumnDefinitions[column];
		var clock = Stopwatch.StartNew();
		var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(8) };
		m_timers[column] = timer;
		timer.Tick += (_, _) => {
			var t = Math.Min(clock.ElapsedMilliseconds / (double)AnimationMs, 1.0);
			var eased = 1 - Math.Pow(1 - t, 3);
			if (t >= 1) {
				timer.Stop();
				definition.Width = final;
				return;
			}

			definition.Width = new GridLength(from + (to - from) * eased);
		};
		timer.Start();
	}

	private void OnLabelTapped(object? sender, TappedEventArgs e) {
		if (sender is not Control { DataContext: PaletteRowLabelVM label } control) return;
		if (label.IsRenaming) return;
		label.BeginRename();

		// wait a frame
		Dispatcher.UIThread.Post(() => {
			if (control.FindDescendantOfType<TextBox>() is not { } box) return;
			box.Focus();
			box.SelectAll();
		}, DispatcherPriority.Render);
	}

	private void OnLabelKeyDown(object? sender, KeyEventArgs e) {
		if (sender is not TextBox { DataContext: PaletteRowLabelVM label }) return;
		if (e.Key == Key.Enter) {
			e.Handled = true;
			label.CommitRename();
		} else if (e.Key == Key.Escape) {
			e.Handled = true;
			label.IsRenaming = false;
		}
	}

	private void OnLabelLostFocus(object? sender, RoutedEventArgs e) {
		// skip firing when IsRenaming is cleared
		if (sender is TextBox { DataContext: PaletteRowLabelVM { IsRenaming: true } label }) label.CommitRename();
	}

	private int CellAt(Point point) {
		var bounds = SwatchGrid.Bounds;
		if (bounds.Width <= 0 || bounds.Height <= 0) return 0;
		var column = Math.Clamp((int)(point.X / bounds.Width * Columns), 0, Columns - 1);
		var row = Math.Clamp((int)(point.Y / bounds.Height * Columns), 0, Columns - 1);
		return row * Columns + column + 1;
	}

	private void OnSwatchPressed(object? sender, PointerPressedEventArgs e) {
		if (Vm is not { } vm || !e.GetCurrentPoint(SwatchGrid).Properties.IsLeftButtonPressed) return;

		var point = e.GetPosition(SwatchGrid);
		var cell = CellAt(point);
		if (cell is < 1 or >= 256) return;

		e.Handled = true;
		e.Pointer.Capture(SwatchGrid);
		m_pressPoint = point;
		m_pressCell = cell;
		m_dragging = false;
		m_dropOffset = 0;

		var modifiers = e.KeyModifiers;
		if (modifiers.HasFlag(KeyModifiers.Control)) m_mode = PressMode.Copy;
		else if (modifiers.HasFlag(KeyModifiers.Shift)) m_mode = PressMode.Move;
		else {
			m_mode = PressMode.Select;
			vm.SelectOnly(cell);
		}
	}

	private void OnSwatchMoved(object? sender, PointerEventArgs e) {
		if (m_mode == PressMode.None || Vm is not { } vm) return;

		var point = e.GetPosition(SwatchGrid);
		if (!m_dragging) {
			var delta = point - m_pressPoint;
			if (delta.X * delta.X + delta.Y * delta.Y < DragThreshold * DragThreshold) return;
			m_dragging = true;

			// dragging something outside the selection drags just that
			if (m_mode != PressMode.Select && !vm.Selection.Contains(m_pressCell)) vm.SelectOnly(m_pressCell);
		}

		var cell = Math.Min(CellAt(point), 255);
		if (m_mode == PressMode.Select) {
			var rowA = (m_pressCell - 1) / Columns;
			var rowB = (cell - 1) / Columns;
			var colA = (m_pressCell - 1) % Columns;
			var colB = (cell - 1) % Columns;
			var indices = Enumerable.Range(Math.Min(rowA, rowB), Math.Abs(rowA - rowB) + 1)
				.SelectMany(row => Enumerable.Range(Math.Min(colA, colB), Math.Abs(colA - colB) + 1)
					.Select(col => row * Columns + col + 1));
			vm.SetSelection(indices, m_pressCell);
			return;
		}

		SetDropPreview(vm, vm.ClampOffset(cell - m_pressCell));
	}

	private void SetDropPreview(PaletteViewModel vm, int offset) {
		ClearDropPreview(vm);
		m_dropOffset = offset;
		if (offset == 0) return;
		foreach (var index in vm.Selection) vm.Swatches[index + offset - 1].IsDropTarget = true;
	}

	private static void ClearDropPreview(PaletteViewModel vm) {
		foreach (var swatch in vm.Swatches) swatch.IsDropTarget = false;
	}

	private void OnSwatchReleased(object? sender, PointerReleasedEventArgs e) {
		EndPress(true);
		e.Pointer.Capture(null);
	}

	private void EndPress(bool apply) {
		var mode = m_mode;
		m_mode = PressMode.None;
		if (Vm is not { } vm || mode == PressMode.None) return;

		ClearDropPreview(vm);
		if (!apply) return;

		if (m_dragging) {
			if (mode == PressMode.Move) vm.MoveSelection(m_dropOffset);
			else if (mode == PressMode.Copy) vm.CopySelection(m_dropOffset);
		} else if (mode == PressMode.Copy) {
			vm.ToggleSelect(m_pressCell);
		} else if (mode == PressMode.Move) {
			vm.SelectRange(vm.SelectedIndex, m_pressCell);
		}

		m_dragging = false;
	}
}
