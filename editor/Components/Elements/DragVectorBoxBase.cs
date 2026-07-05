//
// DragVectorBoxBase.cs by Xein
// 23 Jun 2026
//

using System;
using System.Collections.Generic;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;
using editor.Engine;

namespace editor.Components.Elements;

public abstract class DragVectorBoxBase : Decorator {
	private const double Radius = 6;
	private const double Spacing = 8;
	private const double LabelWidth = 24;
	private const double ThinLabelWidth = 4;
	private const double FontSize = 15;
	private const double BoxPadding = 16;
	private const double SignWidth = 8;
	private const double BoxBreath = 4;

	private static readonly FontFamily Font = new("FiraCode Nerd Font,Consolas,monospace");

	/// <summary>Suffix drawn after every axis number (e.g. "m", "deg")</summary>
	public static readonly StyledProperty<string?> UnitProperty =
		AvaloniaProperty.Register<DragVectorBoxBase, string?>(nameof(Unit));

	public static readonly StyledProperty<double> MinimumProperty =
		AvaloniaProperty.Register<DragVectorBoxBase, double>(nameof(Minimum), double.NegativeInfinity);

	public static readonly StyledProperty<double> MaximumProperty =
		AvaloniaProperty.Register<DragVectorBoxBase, double>(nameof(Maximum), double.PositiveInfinity);

	public static readonly StyledProperty<int> DecimalsProperty =
		AvaloniaProperty.Register<DragVectorBoxBase, int>(nameof(Decimals), 3);

	public static readonly StyledProperty<float> CoarseStepProperty =
		AvaloniaProperty.Register<DragVectorBoxBase, float>(nameof(CoarseStep), 1f);

	public static readonly StyledProperty<float> FineStepProperty =
		AvaloniaProperty.Register<DragVectorBoxBase, float>(nameof(FineStep), 0.001f);

	public static readonly StyledProperty<double> DragSensitivityProperty =
		AvaloniaProperty.Register<DragVectorBoxBase, double>(nameof(DragSensitivity), 4.0);

	private Compaction m_level;

	protected DragVectorBoxBase() {
		HorizontalAlignment = HorizontalAlignment.Stretch;
	}

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

	public double DragSensitivity {
		get => GetValue(DragSensitivityProperty);
		set => SetValue(DragSensitivityProperty, value);
	}

	protected abstract int AxisCount { get; }

	protected abstract AxisSpec[] BuildAxes();

	protected override Size MeasureOverride(Size availableSize) {
		var level = ChooseLevel(availableSize.Width);
		if (Child is null || level != m_level) {
			m_level = level;
			Child = BuildContent(level);
		}

		return base.MeasureOverride(availableSize);
	}

	private IEnumerable<Compaction> Ladder() {
		var max = Math.Max(0, Decimals);
		yield return new Compaction(true, false, max, false);  // full
		yield return new Compaction(false, false, max, false); // drop unit
		yield return new Compaction(false, true, max, false);  // thin label
		for (var d = max - 1; d >= 0; d--)                     // trim decimals one place at a time
			yield return new Compaction(false, true, d, false);
	}

	private Compaction ChooseLevel(double available) {
		var full = new Compaction(true, false, Math.Max(0, Decimals), false);
		if (double.IsInfinity(available)) return full;

		foreach (var level in Ladder())
			if (FitsAt(level, available))
				return level;

		return full with { Vertical = true };
	}

	private bool FitsAt(Compaction level, double available) {
		var axes = BuildAxes();
		var typeface = new Typeface(Font);
		var labelWidth = level.ThinLabel ? ThinLabelWidth : LabelWidth;
		var unit = level.ShowUnit && !string.IsNullOrEmpty(Unit) ? " " + Unit : "";

		var total = Spacing * (axes.Length - 1);
		foreach (var axis in axes) {
			var value = GetValue(axis.Value);
			var text = Math.Abs((double)value).ToString("F" + level.Decimals, CultureInfo.InvariantCulture) + unit;
			var line = new FormattedText(text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
				typeface, FontSize, null);
			var sign = value < 0f ? SignWidth : 0;
			total += labelWidth + sign + line.Width + BoxPadding + BoxBreath;
		}

		return total <= available;
	}

	private Control BuildContent(Compaction level) {
		var vertical = level.Vertical;
		var axes = BuildAxes();
		var grid = new Grid();

		if (vertical) {
			for (var i = 0; i < axes.Length; i++)
				grid.RowDefinitions.Add(new RowDefinition(GridLength.Auto));
		} else {
			grid.ColumnSpacing = Spacing;
			for (var i = 0; i < axes.Length; i++)
				grid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
		}

		for (var i = 0; i < axes.Length; i++) {
			var cell = BuildCell(axes[i], i, axes.Length, level);
			if (vertical) Grid.SetRow(cell, i);
			else Grid.SetColumn(cell, i);
			grid.Children.Add(cell);
		}

		return grid;
	}

	private Control BuildCell(AxisSpec spec, int index, int count, Compaction level) {
		var vertical = level.Vertical;
		var thinLabel = !vertical && level.ThinLabel;
		var showUnit = vertical || level.ShowUnit;
		var decimals = vertical ? Math.Max(0, Decimals) : level.Decimals;

		// CornerRadius args are (tl, tr, br, bl).
		CornerRadius labelCorner, boxCorner;
		if (!vertical) {
			labelCorner = new CornerRadius(Radius, 0, 0, Radius);
			boxCorner = new CornerRadius(0, Radius, Radius, 0);
		} else {
			var first = index == 0;
			var last = index == count - 1;
			labelCorner = new CornerRadius(first ? Radius : 0, 0, 0, last ? Radius : 0);
			boxCorner = new CornerRadius(0, first ? Radius : 0, last ? Radius : 0, 0);
		}

		Border chip;
		if (thinLabel) {
			// keep the axis color as a thin vertical bar
			chip = new Border {
				Background = Brush(spec.ColorKey),
				CornerRadius = labelCorner,
				Width = ThinLabelWidth,
				VerticalAlignment = VerticalAlignment.Stretch
			};
		} else {
			var chipText = new TextBlock {
				Foreground = Brush("Bg1"),
				FontFamily = Font,
				FontSize = FontSize,
				FontWeight = FontWeight.Bold,
				HorizontalAlignment = HorizontalAlignment.Center,
				VerticalAlignment = VerticalAlignment.Center,
				[!TextBlock.TextProperty] = this[!spec.Label]
			};

			chip = new Border {
				Background = Brush(spec.ColorKey),
				CornerRadius = labelCorner,
				MinWidth = LabelWidth,
				VerticalAlignment = VerticalAlignment.Stretch,
				Child = chipText
			};
		}

		var box = new DragFloatBox {
			CornerRadius = boxCorner,
			HorizontalAlignment = HorizontalAlignment.Stretch,
			[!!DragFloatBox.ValueProperty] = this[!!spec.Value],
			[!DragNumberBoxBase.MinimumProperty] = this[!MinimumProperty],
			[!DragNumberBoxBase.MaximumProperty] = this[!MaximumProperty],
			[!DragNumberBoxBase.DragSensitivityProperty] = this[!DragSensitivityProperty],
			[!DragFloatBox.CoarseStepProperty] = this[!CoarseStepProperty],
			[!DragFloatBox.FineStepProperty] = this[!FineStepProperty]
		};
		if (showUnit) box[!DragNumberBoxBase.UnitProperty] = this[!UnitProperty];
		box.Decimals = decimals;

		var cell = new Grid {
			ColumnDefinitions = {
				new ColumnDefinition(GridLength.Auto),
				new ColumnDefinition(GridLength.Star)
			}
		};
		Grid.SetColumn(chip, 0);
		Grid.SetColumn(box, 1);
		cell.Children.Add(chip);
		cell.Children.Add(box);
		return cell;
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == DecimalsProperty) {
			Child = null;
			InvalidateMeasure();
		}
	}

	private static IBrush? Brush(string key) {
		if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true)
			return r as IBrush;
		Log.Warn($"Brush '{key}' not found in resources");
		return null;
	}

	private readonly record struct Compaction(bool ShowUnit, bool ThinLabel, int Decimals, bool Vertical);

	protected readonly record struct AxisSpec(
		StyledProperty<string> Label,
		string ColorKey,
		StyledProperty<float> Value);
}
