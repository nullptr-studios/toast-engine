//
// DragIntBox.cs by Xein
// 23 Jun 2026
//

using System;
using System.Globalization;
using Avalonia;
using Avalonia.Data;

namespace editor.Components.Elements;

public sealed class DragIntBox : DragNumberBoxBase {
	public static readonly StyledProperty<int> ValueProperty =
		AvaloniaProperty.Register<DragIntBox, int>(nameof(Value), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<int> StepProperty =
		AvaloniaProperty.Register<DragIntBox, int>(nameof(Step), 1);

	public int Value {
		get => GetValue(ValueProperty);
		set => SetValue(ValueProperty, value);
	}

	public int Step {
		get => GetValue(StepProperty);
		set => SetValue(StepProperty, value);
	}

	protected override double CoarseStepAmount => Step;
	protected override double FineStepAmount => Step;

	protected override double GetDouble() {
		return Value;
	}

	protected override void SetDouble(double value) {
		Value = (int)Math.Round(value);
	}

	protected override string RawEditText() {
		return Value.ToString(CultureInfo.InvariantCulture);
	}

	protected override bool TryParseEdit(string text, out double value) {
		value = 0;
		if (!int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var i)) return false;
		value = i;
		return true;
	}

	protected override void RebuildDisplay() {
		var v = Value;
		var integer = Math.Abs((long)v).ToString(CultureInfo.InvariantCulture);
		SetDisplayParts(v < 0 ? "-" : "", integer, "", "");
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == ValueProperty) OnValueChanged();
	}
}
