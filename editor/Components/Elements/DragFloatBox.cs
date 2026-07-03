//
// DragFloatBox.cs by Xein
// 23 Jun 2026
//

using System;
using System.Globalization;
using Avalonia;
using Avalonia.Data;

namespace editor.Components.Elements;

public sealed class DragFloatBox : DragNumberBoxBase {
	public static readonly StyledProperty<float> ValueProperty =
		AvaloniaProperty.Register<DragFloatBox, float>(nameof(Value), defaultBindingMode: BindingMode.TwoWay);

	/// <summary>Number of decimal places always shown</summary>
	public static readonly StyledProperty<int> DecimalsProperty =
		AvaloniaProperty.Register<DragFloatBox, int>(nameof(Decimals), 3);

	public static readonly StyledProperty<float> CoarseStepProperty =
		AvaloniaProperty.Register<DragFloatBox, float>(nameof(CoarseStep), 1f);

	public static readonly StyledProperty<float> FineStepProperty =
		AvaloniaProperty.Register<DragFloatBox, float>(nameof(FineStep), 0.001f);

	public float Value {
		get => GetValue(ValueProperty);
		set => SetValue(ValueProperty, value);
	}

	public int Decimals {
		get => GetValue(DecimalsProperty);
		set => SetValue(DecimalsProperty, value);
	}

	public float CoarseStep {
		get => GetValue(CoarseStepProperty);
		set => SetValue(CoarseStepProperty, value);
	}

	public float FineStep {
		get => GetValue(FineStepProperty);
		set => SetValue(FineStepProperty, value);
	}

	protected override double CoarseStepAmount => CoarseStep;
	protected override double FineStepAmount => FineStep;

	protected override double GetDouble() => Value;

	protected override void SetDouble(double value) => Value = (float)value;

	protected override string RawEditText() => Value.ToString(CultureInfo.InvariantCulture);

	protected override bool TryParseEdit(string text, out double value) {
		value = 0;
		if (!float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var f)) return false;
		value = f;
		return true;
	}

	protected override void RebuildDisplay() {
		var v = Value;
		var decimals = Math.Max(0, Decimals);
		var negative = v < 0f;
		var magnitude = Math.Abs((double)v);

		var formatted = magnitude.ToString("F" + decimals, CultureInfo.InvariantCulture);
		var dot = formatted.IndexOf('.');

		string integer, digits;
		if (dot >= 0) {
			integer = formatted[..dot];
			digits = formatted[(dot + 1)..];
		}
		else {
			integer = formatted;
			digits = "";
		}

		SetDisplayParts(negative ? "-" : "", integer, decimals > 0 ? "." : "", digits);
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == ValueProperty || change.Property == DecimalsProperty) OnValueChanged();
	}
}
