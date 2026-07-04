//
// DragNumberBoxBase.cs by Xein
// 23 Jun 2026
//

using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Threading;

namespace editor.Components.Elements;

public abstract class DragNumberBoxBase : TemplatedControl {
	// Drag state
	private const double DragThreshold = 3.0;

	/// <summary>Optional suffix drawn after the number (e.g. "m", "kg")</summary>
	public static readonly StyledProperty<string?> UnitProperty =
		AvaloniaProperty.Register<DragNumberBoxBase, string?>(nameof(Unit));

	public static readonly StyledProperty<double> MinimumProperty =
		AvaloniaProperty.Register<DragNumberBoxBase, double>(nameof(Minimum), double.NegativeInfinity);

	public static readonly StyledProperty<double> MaximumProperty =
		AvaloniaProperty.Register<DragNumberBoxBase, double>(nameof(Maximum), double.PositiveInfinity);

	/// <summary>Pixels the pointer must travel to move the value by one step</summary>
	public static readonly StyledProperty<double> DragSensitivityProperty =
		AvaloniaProperty.Register<DragNumberBoxBase, double>(nameof(DragSensitivity), 4.0);

	public static readonly StyledProperty<IBrush?> EditBorderBrushProperty =
		AvaloniaProperty.Register<DragNumberBoxBase, IBrush?>(nameof(EditBorderBrush));

	public static readonly DirectProperty<DragNumberBoxBase, bool> IsEditingProperty =
		AvaloniaProperty.RegisterDirect<DragNumberBoxBase, bool>(nameof(IsEditing), o => o.m_isEditing);

	public static readonly DirectProperty<DragNumberBoxBase, string> DisplaySignProperty =
		AvaloniaProperty.RegisterDirect<DragNumberBoxBase, string>(nameof(DisplaySign), o => o.m_displaySign);

	public static readonly DirectProperty<DragNumberBoxBase, string> DisplayIntegerProperty =
		AvaloniaProperty.RegisterDirect<DragNumberBoxBase, string>(nameof(DisplayInteger), o => o.m_displayInteger);

	public static readonly DirectProperty<DragNumberBoxBase, string> DisplayPointProperty =
		AvaloniaProperty.RegisterDirect<DragNumberBoxBase, string>(nameof(DisplayPoint), o => o.m_displayPoint);

	public static readonly DirectProperty<DragNumberBoxBase, string> DisplayDigitsProperty =
		AvaloniaProperty.RegisterDirect<DragNumberBoxBase, string>(nameof(DisplayDigits), o => o.m_displayDigits);

	private string m_displayDigits = "";

	private string m_displayInteger = "0";

	private string m_displayPoint = "";

	private string m_displaySign = "";
	private bool m_dragFine;
	private bool m_dragMoved;
	private double m_dragStartValue;
	private double m_dragStartX;
	private bool m_dragging;
	private TextBox? m_editor;
	private Control? m_fractionRegion;
	private Control? m_integerRegion;

	private bool m_isEditing;

	// Template parts
	private Control? m_root;

	public string? Unit {
		get => GetValue(UnitProperty);
		set => SetValue(UnitProperty, value);
	}

	public double Minimum {
		get => GetValue(MinimumProperty);
		set => SetValue(MinimumProperty, value);
	}

	public double Maximum {
		get => GetValue(MaximumProperty);
		set => SetValue(MaximumProperty, value);
	}

	public double DragSensitivity {
		get => GetValue(DragSensitivityProperty);
		set => SetValue(DragSensitivityProperty, value);
	}

	public IBrush? EditBorderBrush {
		get => GetValue(EditBorderBrushProperty);
		set => SetValue(EditBorderBrushProperty, value);
	}

	public bool IsEditing {
		get => m_isEditing;
		private set => SetAndRaise(IsEditingProperty, ref m_isEditing, value);
	}

	public string DisplaySign => m_displaySign;
	public string DisplayInteger => m_displayInteger;
	public string DisplayPoint => m_displayPoint;
	public string DisplayDigits => m_displayDigits;

	protected override Type StyleKeyOverride => typeof(DragNumberBoxBase);

	protected abstract double CoarseStepAmount { get; }

	protected abstract double FineStepAmount { get; }

	protected abstract double GetDouble();

	protected abstract void SetDouble(double value);

	protected abstract string RawEditText();

	protected abstract bool TryParseEdit(string text, out double value);
	protected abstract void RebuildDisplay();

	protected double ClampDrag(double value) {
		var min = Minimum;
		var max = Maximum;
		if (min > max) return value;
		return Math.Clamp(value, min, max);
	}

	protected void SetDisplayParts(string sign, string integer, string point, string digits) {
		SetAndRaise(DisplaySignProperty, ref m_displaySign, sign);
		SetAndRaise(DisplayIntegerProperty, ref m_displayInteger, integer);
		SetAndRaise(DisplayPointProperty, ref m_displayPoint, point);
		SetAndRaise(DisplayDigitsProperty, ref m_displayDigits, digits);
	}

	protected void OnValueChanged() {
		RebuildDisplay();
	}

	protected override void OnApplyTemplate(TemplateAppliedEventArgs e) {
		base.OnApplyTemplate(e);
		DetachHandlers();

		m_root = e.NameScope.Find<Control>("PART_Root");
		m_integerRegion = e.NameScope.Find<Control>("PART_Integer");
		m_fractionRegion = e.NameScope.Find<Control>("PART_Fraction");
		m_editor = e.NameScope.Find<TextBox>("PART_Editor");

		if (m_root != null)
			m_root.Tapped += OnRootTapped;

		if (m_integerRegion != null) {
			m_integerRegion.PointerPressed += OnIntegerPressed;
			m_integerRegion.PointerMoved += OnDragMoved;
			m_integerRegion.PointerReleased += OnDragReleased;
			m_integerRegion.PointerWheelChanged += OnIntegerWheel;
		}

		if (m_fractionRegion != null) {
			m_fractionRegion.PointerPressed += OnFractionPressed;
			m_fractionRegion.PointerMoved += OnDragMoved;
			m_fractionRegion.PointerReleased += OnDragReleased;
			m_fractionRegion.PointerWheelChanged += OnFractionWheel;
		}

		if (m_editor != null) {
			m_editor.KeyDown += OnEditorKeyDown;
			m_editor.LostFocus += OnEditorLostFocus;
		}

		RebuildDisplay();
	}

	private void DetachHandlers() {
		if (m_root != null)
			m_root.Tapped -= OnRootTapped;

		if (m_integerRegion != null) {
			m_integerRegion.PointerPressed -= OnIntegerPressed;
			m_integerRegion.PointerMoved -= OnDragMoved;
			m_integerRegion.PointerReleased -= OnDragReleased;
			m_integerRegion.PointerWheelChanged -= OnIntegerWheel;
		}

		if (m_fractionRegion != null) {
			m_fractionRegion.PointerPressed -= OnFractionPressed;
			m_fractionRegion.PointerMoved -= OnDragMoved;
			m_fractionRegion.PointerReleased -= OnDragReleased;
			m_fractionRegion.PointerWheelChanged -= OnFractionWheel;
		}

		if (m_editor != null) {
			m_editor.KeyDown -= OnEditorKeyDown;
			m_editor.LostFocus -= OnEditorLostFocus;
		}
	}

	private void OnIntegerPressed(object? sender, PointerPressedEventArgs e) {
		BeginDrag(sender, e, false);
	}

	private void OnFractionPressed(object? sender, PointerPressedEventArgs e) {
		BeginDrag(sender, e, true);
	}

	private void OnRootTapped(object? sender, TappedEventArgs e) {
		if (!IsEnabled || IsEditing) return;
		BeginEdit();
		e.Handled = true;
	}

	private void BeginDrag(object? sender, PointerPressedEventArgs e, bool fine) {
		if (!IsEnabled || IsEditing) return;
		if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;

		m_dragging = true;
		m_dragFine = fine;
		m_dragMoved = false;
		m_dragStartX = e.GetPosition(this).X;
		m_dragStartValue = GetDouble();
		if (sender is IInputElement element) e.Pointer.Capture(element);
		e.Handled = true;
	}

	private void OnDragMoved(object? sender, PointerEventArgs e) {
		if (!m_dragging) return;

		var dx = e.GetPosition(this).X - m_dragStartX;
		if (!m_dragMoved && Math.Abs(dx) < DragThreshold) return;
		m_dragMoved = true;

		var step = m_dragFine ? FineStepAmount : CoarseStepAmount;
		var ticks = Math.Round(dx / DragSensitivity);
		SetDouble(ClampDrag(m_dragStartValue + ticks * step));
	}

	private void OnDragReleased(object? sender, PointerReleasedEventArgs e) {
		if (!m_dragging) return;
		m_dragging = false;
		e.Pointer.Capture(null);
		if (!m_dragMoved) BeginEdit();
		e.Handled = true;
	}

	private void OnIntegerWheel(object? sender, PointerWheelEventArgs e) {
		Scroll(e, false);
	}

	private void OnFractionWheel(object? sender, PointerWheelEventArgs e) {
		Scroll(e, true);
	}

	private void Scroll(PointerWheelEventArgs e, bool fine) {
		if (!IsEnabled || IsEditing) return;
		var dir = Math.Sign(e.Delta.Y);
		if (dir == 0) return;

		var step = fine ? FineStepAmount : CoarseStepAmount;
		SetDouble(ClampDrag(GetDouble() + dir * step));
		e.Handled = true;
	}

	private void BeginEdit() {
		if (!IsEnabled || m_editor == null) return;
		IsEditing = true;
		m_editor.Text = RawEditText();
		Dispatcher.UIThread.Post(() => {
			m_editor.Focus();
			m_editor.SelectAll();
		});
	}

	private void CommitEdit() {
		if (!IsEditing) return;
		if (m_editor != null && TryParseEdit(m_editor.Text ?? "", out var value)) SetDouble(ClampDrag(value));
		IsEditing = false;
	}

	private void CancelEdit() {
		if (!IsEditing) return;
		IsEditing = false;
	}

	private void OnEditorKeyDown(object? sender, KeyEventArgs e) {
		switch (e.Key) {
			case Key.Enter:
				CommitEdit();
				e.Handled = true;
				break;
			case Key.Escape:
				CancelEdit();
				e.Handled = true;
				break;
		}
	}

	private void OnEditorLostFocus(object? sender, RoutedEventArgs e) {
		CommitEdit();
	}
}
