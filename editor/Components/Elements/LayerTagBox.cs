//
// LayerTagBox.cs by Xein
// 12 Oct 2026
//

using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Data;
using Avalonia.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Engine;

namespace editor.Components.Elements;

public enum LayerState {
	Disabled,
	Included,
	Excluded
}

public readonly record struct LayerMasks(uint Included, uint Excluded);

public sealed partial class LayerCell : ObservableObject {
	[ObservableProperty] [NotifyPropertyChangedFor(nameof(IsIncluded), nameof(IsExcluded), nameof(IsDisabled))]
	private LayerState m_state;

	[ObservableProperty] private string m_text = "";
	[ObservableProperty] private string m_tooltip = "";

	public LayerCell(int index) {
		Index = index;
	}

	public int Index { get; }
	public bool IsIncluded => State == LayerState.Included;
	public bool IsExcluded => State == LayerState.Excluded;
	public bool IsDisabled => State == LayerState.Disabled;
}

public sealed class LayerTagBox : TemplatedControl {
	public const int Columns = 4;
	public const int MinRows = 1;
	public const int MaxRows = PhysicsLayers.Count / Columns;
	private const double RowStep = 36.0; // cell height + row spacing

	public static readonly StyledProperty<uint> IncludedProperty =
		AvaloniaProperty.Register<LayerTagBox, uint>(nameof(Included));

	public static readonly StyledProperty<uint> ExcludedProperty =
		AvaloniaProperty.Register<LayerTagBox, uint>(nameof(Excluded));

	public static readonly StyledProperty<int> RowsProperty =
		AvaloniaProperty.Register<LayerTagBox, int>(nameof(Rows), 2, defaultBindingMode: BindingMode.TwoWay,
			coerce: (_, value) => Math.Clamp(value, MinRows, MaxRows));

	public static readonly StyledProperty<ICommand?> CommandProperty =
		AvaloniaProperty.Register<LayerTagBox, ICommand?>(nameof(Command));

	private readonly LayerCell[] m_cells;
	private double m_dragStartY;
	private int m_dragStartRows;
	private bool m_dragging;
	private Control? m_handle;
	private ItemsControl? m_items;

	// What the last click asked for
	private uint m_included;
	private uint m_excluded;

	public LayerTagBox() {
		m_cells = Enumerable.Range(0, PhysicsLayers.Count).Select(i => new LayerCell(i)).ToArray();
		RefreshNames();
		RefreshStates();
		RefreshVisible();
	}

	public uint Included {
		get => GetValue(IncludedProperty);
		set => SetValue(IncludedProperty, value);
	}

	public uint Excluded {
		get => GetValue(ExcludedProperty);
		set => SetValue(ExcludedProperty, value);
	}

	public int Rows {
		get => GetValue(RowsProperty);
		set => SetValue(RowsProperty, value);
	}

	public ICommand? Command {
		get => GetValue(CommandProperty);
		set => SetValue(CommandProperty, value);
	}

	public ObservableCollection<LayerCell> VisibleCells { get; } = [];

	protected override Type StyleKeyOverride => typeof(LayerTagBox);

	protected override void OnApplyTemplate(TemplateAppliedEventArgs e) {
		base.OnApplyTemplate(e);
		DetachHandlers();

		m_items = e.NameScope.Find<ItemsControl>("PART_Cells");
		m_handle = e.NameScope.Find<Control>("PART_Handle");
		if (m_items != null) {
			m_items.Tapped += OnCellTapped;
			m_items.DoubleTapped += OnCellTapped;
		}
		if (m_handle != null) {
			m_handle.PointerPressed += OnHandlePressed;
			m_handle.PointerMoved += OnHandleMoved;
			m_handle.PointerReleased += OnHandleReleased;
			m_handle.PointerCaptureLost += OnHandleCaptureLost;
		}
	}

	protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e) {
		base.OnAttachedToVisualTree(e);
		RefreshNames(); // names may have changed since the box was made
	}

	private void DetachHandlers() {
		if (m_items != null) {
			m_items.Tapped -= OnCellTapped;
			m_items.DoubleTapped -= OnCellTapped;
		}
		if (m_handle != null) {
			m_handle.PointerPressed -= OnHandlePressed;
			m_handle.PointerMoved -= OnHandleMoved;
			m_handle.PointerReleased -= OnHandleReleased;
			m_handle.PointerCaptureLost -= OnHandleCaptureLost;
		}
	}

	private void OnCellTapped(object? sender, TappedEventArgs e) {
		if (!IsEnabled) return;
		for (var visual = e.Source as StyledElement; visual != null; visual = visual.Parent) {
			if (visual.DataContext is not LayerCell cell) continue;
			Toggle(cell.Index);
			e.Handled = true;
			return;
		}
	}

	private void Toggle(int index) {
		var bit = 1u << index;
		var included = m_included;
		var excluded = m_excluded;

		// disabled -> included -> excluded -> disabled
		if ((excluded & bit) != 0) {
			excluded &= ~bit;
		} else if ((included & bit) != 0) {
			included &= ~bit;
			excluded |= bit;
		} else {
			included |= bit;
		}

		m_included = included;
		m_excluded = excluded;
		RefreshStates();

		var masks = new LayerMasks(included, excluded);
		if (Command?.CanExecute(masks) == true) Command.Execute(masks);
	}

	private void OnHandlePressed(object? sender, PointerPressedEventArgs e) {
		if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		m_dragging = true;
		m_dragStartY = e.GetPosition(this).Y;
		m_dragStartRows = Rows;
		e.Pointer.Capture(m_handle);
		e.Handled = true;
	}

	private void OnHandleMoved(object? sender, PointerEventArgs e) {
		if (!m_dragging) return;
		var steps = (int)Math.Round((e.GetPosition(this).Y - m_dragStartY) / RowStep);
		Rows = m_dragStartRows + steps;
	}

	private void OnHandleReleased(object? sender, PointerReleasedEventArgs e) {
		if (!m_dragging) return;
		m_dragging = false;
		e.Pointer.Capture(null);
		e.Handled = true;
	}

	private void OnHandleCaptureLost(object? sender, PointerCaptureLostEventArgs e) {
		m_dragging = false;
	}

	public void RefreshNames() {
		foreach (var cell in m_cells) {
			cell.Text = PhysicsLayers.Abbreviation(cell.Index);
			cell.Tooltip = $"{cell.Index}: {PhysicsLayers.Name(cell.Index)}";
		}
	}

	private void RefreshStates() {
		foreach (var cell in m_cells) {
			var bit = 1u << cell.Index;
			cell.State = (m_excluded & bit) != 0 ? LayerState.Excluded
				: (m_included & bit) != 0 ? LayerState.Included
				: LayerState.Disabled;
		}
	}

	private void RefreshVisible() {
		var count = Rows * Columns;
		while (VisibleCells.Count > count) VisibleCells.RemoveAt(VisibleCells.Count - 1);
		while (VisibleCells.Count < count) VisibleCells.Add(m_cells[VisibleCells.Count]);
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == IncludedProperty || change.Property == ExcludedProperty) {
			m_included = Included;
			m_excluded = Excluded;
			RefreshStates();
		}
		else if (change.Property == RowsProperty) RefreshVisible();
	}
}
