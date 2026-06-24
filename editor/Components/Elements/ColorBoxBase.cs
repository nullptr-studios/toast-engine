//
// ColorBoxBase.cs by Xein
// 24 Jun 2026
//

using System;
using System.Globalization;
using System.Text;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;
using editor.Assets;

namespace editor.Components.Elements;

public abstract class ColorBoxBase : TemplatedControl {
	private const double DragThreshold = 3.0;

	private const double RootPadding = 16;
	private const double SwatchSize = 20;
	private const double Spacing = 8;
	private const double BoxBreath = 6;

	private static readonly FontFamily Font = new("FiraCode Nerd Font,Consolas,monospace");
	private static readonly Cursor DragCursor = new(StandardCursorType.SizeWestEast);

	public static readonly StyledProperty<float> RProperty =
		AvaloniaProperty.Register<ColorBoxBase, float>(nameof(R), 1f, defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<float> GProperty =
		AvaloniaProperty.Register<ColorBoxBase, float>(nameof(G), 1f, defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<float> BProperty =
		AvaloniaProperty.Register<ColorBoxBase, float>(nameof(B), 1f, defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<int> DecimalsProperty =
		AvaloniaProperty.Register<ColorBoxBase, int>(nameof(Decimals), 2);

	public static readonly StyledProperty<float> CoarseStepProperty =
		AvaloniaProperty.Register<ColorBoxBase, float>(nameof(CoarseStep), 0.1f);

	public static readonly StyledProperty<float> FineStepProperty =
		AvaloniaProperty.Register<ColorBoxBase, float>(nameof(FineStep), 0.01f);

	public static readonly StyledProperty<double> DragSensitivityProperty =
		AvaloniaProperty.Register<ColorBoxBase, double>(nameof(DragSensitivity), 4.0);

	public static readonly StyledProperty<double> MinimumProperty =
		AvaloniaProperty.Register<ColorBoxBase, double>(nameof(Minimum), 0.0);

	public static readonly StyledProperty<double> MaximumProperty =
		AvaloniaProperty.Register<ColorBoxBase, double>(nameof(Maximum), double.PositiveInfinity);

	private bool m_numbersVisible = true;

	public static readonly DirectProperty<ColorBoxBase, bool> NumbersVisibleProperty =
		AvaloniaProperty.RegisterDirect<ColorBoxBase, bool>(nameof(NumbersVisible), o => o.m_numbersVisible);

	private IBrush? m_swatchBrush;

	public static readonly DirectProperty<ColorBoxBase, IBrush?> SwatchBrushProperty =
		AvaloniaProperty.RegisterDirect<ColorBoxBase, IBrush?>(nameof(SwatchBrush), o => o.m_swatchBrush);

	private bool m_showChecker;

	public static readonly DirectProperty<ColorBoxBase, bool> ShowCheckerProperty =
		AvaloniaProperty.RegisterDirect<ColorBoxBase, bool>(nameof(ShowChecker), o => o.m_showChecker);

	// Template parts
	private Control? m_root;
	private Border? m_swatch;
	private Border? m_checker;
	private Panel? m_value;

	// Inline number zones
	private int m_builtDecimals = -1;
	private TextBlock[]? m_intBlocks;
	private TextBlock[]? m_fracBlocks;

	// Drag state
	private bool m_dragging;
	private int m_dragChannel;
	private bool m_dragFine;
	private bool m_dragMoved;
	private double m_dragStartX;
	private float m_dragStartValue;

	// Picker
	private const double IntensityMax = 10.0;
	private Flyout? m_pickerFlyout;
	private ColorView? m_pickerView;
	private Slider? m_intensitySlider;
	private bool m_pickerSyncing;

	private IBrush? m_hoverBrush;
	private IBrush? m_mutedBrush;
	private IBrush HoverBrush => m_hoverBrush ??= Resource("Bg3") ?? Brushes.Transparent;
	private IBrush MutedBrush => m_mutedBrush ??= Resource("TextMuted") ?? Brushes.Gray;

	public float R {
		get => GetValue(RProperty);
		set => SetValue(RProperty, value);
	}

	public float G {
		get => GetValue(GProperty);
		set => SetValue(GProperty, value);
	}

	public float B {
		get => GetValue(BProperty);
		set => SetValue(BProperty, value);
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

	public double Minimum {
		get => GetValue(MinimumProperty);
		set => SetValue(MinimumProperty, value);
	}

	public double Maximum {
		get => GetValue(MaximumProperty);
		set => SetValue(MaximumProperty, value);
	}

	public bool NumbersVisible => m_numbersVisible;
	public IBrush? SwatchBrush => m_swatchBrush;
	public bool ShowChecker => m_showChecker;

	protected override Type StyleKeyOverride => typeof(ColorBoxBase);

	protected abstract bool HasAlpha { get; }

	protected virtual float GetAlpha() => 1f;

	protected virtual void SetAlpha(float value) { }

	private int ChannelCount => HasAlpha ? 4 : 3;

	protected ColorBoxBase() {
		SetAndRaise(ShowCheckerProperty, ref m_showChecker, HasAlpha);
		UpdateSwatch();
	}

	protected void RefreshDisplay() {
		UpdateSwatch();
		InvalidateMeasure();
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == RProperty || change.Property == GProperty || change.Property == BProperty ||
		    change.Property == DecimalsProperty)
			RefreshDisplay();
	}

	protected override Size MeasureOverride(Size availableSize) {
		UpdateDisplay(availableSize.Width);
		return base.MeasureOverride(availableSize);
	}

	protected override void OnApplyTemplate(TemplateAppliedEventArgs e) {
		base.OnApplyTemplate(e);
		if (m_root != null) m_root.Tapped -= OnRootTapped;

		m_root = e.NameScope.Find<Control>("PART_Root");
		m_swatch = e.NameScope.Find<Border>("PART_Swatch");
		m_checker = e.NameScope.Find<Border>("PART_Checker");
		m_value = e.NameScope.Find<Panel>("PART_Value");

		if (m_checker != null) m_checker.Background = CheckerBrush.Instance;
		if (m_root != null) m_root.Tapped += OnRootTapped;

		m_builtDecimals = -1; // force a rebuild against the new value panel
		UpdateSwatch();
	}

	private bool IsAlpha(int channel) => HasAlpha && channel == ChannelCount - 1;

	private float GetChannel(int channel) => channel switch {
		0 => R,
		1 => G,
		2 => B,
		_ => GetAlpha()
	};

	private void SetChannel(int channel, float value) {
		switch (channel) {
			case 0:
				R = value;
				break;
			case 1:
				G = value;
				break;
			case 2:
				B = value;
				break;
			default:
				SetAlpha(value);
				break;
		}
	}

	private double ClampChannel(int channel, double value) {
		if (IsAlpha(channel)) return Math.Clamp(value, 0.0, 1.0);
		var min = Minimum;
		var max = Maximum;
		if (min > max) return value;
		return Math.Clamp(value, min, max);
	}

	private void UpdateDisplay(double availableWidth) {
		var maxDecimals = Math.Max(0, Decimals);
		int decimals;
		bool visible;

		if (double.IsInfinity(availableWidth)) {
			decimals = maxDecimals;
			visible = true;
		}
		else {
			var valueWidth = availableWidth - RootPadding - SwatchSize - Spacing;
			var floor = maxDecimals == 0 ? 0 : 1; // keep at least one decimal, else hide
			decimals = -1;
			for (var d = maxDecimals; d >= floor; d--)
				if (MeasureText(Format(d)) <= valueWidth) {
					decimals = d;
					break;
				}

			visible = decimals >= 0;
			if (!visible) decimals = floor;
		}

		SetNumbersVisible(visible);
		if (m_value == null || !visible) return;

		if (decimals != m_builtDecimals) {
			RebuildValue(decimals);
			m_builtDecimals = decimals;
		}

		UpdateChannelTexts(decimals);
	}

	private void RebuildValue(int decimals) {
		var value = m_value!;
		value.Children.Clear();

		var count = ChannelCount;
		m_intBlocks = new TextBlock[count];
		m_fracBlocks = new TextBlock[count];

		for (var i = 0; i < count; i++) {
			if (i > 0) {
				var sep = new TextBlock { Text = " ", VerticalAlignment = VerticalAlignment.Center };
				sep[!TextBlock.ForegroundProperty] = this[!ForegroundProperty];
				value.Children.Add(sep);
			}

			var integer = new TextBlock { VerticalAlignment = VerticalAlignment.Center };
			integer[!TextBlock.ForegroundProperty] = this[!ForegroundProperty];

			var fraction = new TextBlock {
				VerticalAlignment = VerticalAlignment.Bottom,
				FontSize = FontSize * 0.85,
				Foreground = MutedBrush
			};

			m_intBlocks[i] = integer;
			m_fracBlocks[i] = fraction;

			var zones = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
			zones.Children.Add(MakeZone(integer, i, false));
			if (decimals > 0) zones.Children.Add(MakeZone(fraction, i, true));

			var underline = new Border {
				Height = 2,
				CornerRadius = new CornerRadius(1),
				Background = AxisBrush(i),
				HorizontalAlignment = HorizontalAlignment.Stretch,
				VerticalAlignment = VerticalAlignment.Bottom,
				Margin = new Thickness(0, 0, 0, -4),
				IsHitTestVisible = false
			};

			var group = new Panel { VerticalAlignment = VerticalAlignment.Stretch };
			group.Children.Add(zones);
			group.Children.Add(underline);
			value.Children.Add(group);
		}
	}

	private IBrush AxisBrush(int channel) {
		var key = channel switch {
			0 => "Red",
			1 => "Green",
			2 => "Blue",
			_ => "Magenta"
		};
		return Resource(key) ?? Brushes.Gray;
	}

	private Border MakeZone(Control child, int channel, bool fine) {
		var zone = new Border {
			Background = Brushes.Transparent,
			Cursor = DragCursor,
			Child = child
		};

		zone.PointerEntered += (_, _) => {
			if (IsEnabled) zone.Background = HoverBrush;
		};
		zone.PointerExited += (_, _) => zone.Background = Brushes.Transparent;
		zone.PointerPressed += (_, e) => BeginDrag(zone, e, channel, fine);
		zone.PointerMoved += OnDragMoved;
		zone.PointerReleased += OnDragReleased;
		zone.PointerWheelChanged += (_, e) => ScrollChannel(e, channel, fine);
		return zone;
	}

	private void UpdateChannelTexts(int decimals) {
		if (m_intBlocks == null || m_fracBlocks == null) return;

		for (var i = 0; i < m_intBlocks.Length; i++) {
			var formatted = ((double)GetChannel(i)).ToString("F" + decimals, CultureInfo.InvariantCulture);
			var dot = formatted.IndexOf('.');
			if (dot >= 0) {
				m_intBlocks[i].Text = formatted[..dot];
				m_fracBlocks[i].Text = formatted[dot..];
			}
			else {
				m_intBlocks[i].Text = formatted;
				m_fracBlocks[i].Text = "";
			}
		}
	}

	private string Format(int decimals) {
		var sb = new StringBuilder();
		for (var i = 0; i < ChannelCount; i++) {
			if (i > 0) sb.Append(' ');
			sb.Append(((double)GetChannel(i)).ToString("F" + decimals, CultureInfo.InvariantCulture));
		}

		return sb.ToString();
	}

	private double MeasureText(string text) {
		var typeface = new Typeface(FontFamily ?? Font);
		var formatted = new FormattedText(text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
			typeface, FontSize, null);
		return formatted.Width + BoxBreath;
	}

	private void UpdateSwatch() {
		var max = MathF.Max(R, MathF.Max(G, B));
		var scale = max > 1f ? 1f / max : 1f;
		var a = HasAlpha ? ToByte(GetAlpha()) : (byte)255;
		var color = Color.FromArgb(a, ToByte(R * scale), ToByte(G * scale), ToByte(B * scale));
		SetAndRaise(SwatchBrushProperty, ref m_swatchBrush, new SolidColorBrush(color));
	}

	private static byte ToByte(float value) => (byte)Math.Clamp(MathF.Round(value * 255f), 0f, 255f);

	private void SetNumbersVisible(bool value) => SetAndRaise(NumbersVisibleProperty, ref m_numbersVisible, value);

	private static IBrush? Resource(string key) {
		if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true)
			return r as IBrush;
		return null;
	}

	private void BeginDrag(Control zone, PointerPressedEventArgs e, int channel, bool fine) {
		if (!IsEnabled) return;
		if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;

		m_dragging = true;
		m_dragChannel = channel;
		m_dragFine = fine;
		m_dragMoved = false;
		m_dragStartX = e.GetPosition(this).X;
		m_dragStartValue = GetChannel(channel);
		e.Pointer.Capture(zone);
		e.Handled = true;
	}

	private void OnDragMoved(object? sender, PointerEventArgs e) {
		if (!m_dragging) return;

		var dx = e.GetPosition(this).X - m_dragStartX;
		if (!m_dragMoved && Math.Abs(dx) < DragThreshold) return;
		m_dragMoved = true;

		var step = m_dragFine ? FineStep : CoarseStep;
		var delta = Math.Round(dx / DragSensitivity) * step;
		SetChannel(m_dragChannel, (float)ClampChannel(m_dragChannel, m_dragStartValue + delta));
	}

	private void OnDragReleased(object? sender, PointerReleasedEventArgs e) {
		if (!m_dragging) return;
		m_dragging = false;
		e.Pointer.Capture(null);
		if (!m_dragMoved) OpenPicker();
		e.Handled = true;
	}

	private void ScrollChannel(PointerWheelEventArgs e, int channel, bool fine) {
		if (!IsEnabled) return;
		var dir = Math.Sign(e.Delta.Y);
		if (dir == 0) return;

		var step = (fine ? FineStep : CoarseStep) * dir;
		SetChannel(channel, (float)ClampChannel(channel, GetChannel(channel) + step));
		e.Handled = true;
	}

	private void OnRootTapped(object? sender, TappedEventArgs e) {
		if (!IsEnabled) return;
		OpenPicker();
		e.Handled = true;
	}

	private void OpenPicker() {
		if (!IsEnabled) return;

		if (m_pickerView == null) BuildPicker();

		// The picker is 8 bit, so split the channels into a 0..1 chroma
		var max = MathF.Max(R, MathF.Max(G, B));
		var intensity = max > 1f ? max : 1f;
		var inv = 1f / intensity;

		m_pickerSyncing = true;
		m_pickerView!.Color = Color.FromArgb(
			HasAlpha ? ToByte(GetAlpha()) : (byte)255, ToByte(R * inv), ToByte(G * inv), ToByte(B * inv));
		m_intensitySlider!.Value = intensity;
		m_pickerSyncing = false;

		m_pickerFlyout!.ShowAt((Control?)m_swatch ?? this);
	}

	private void BuildPicker() {
		m_pickerView = new ColorView {
			IsAlphaEnabled = HasAlpha,
			IsAlphaVisible = HasAlpha,
			IsColorPaletteVisible = false,
			ColorSpectrumShape = ColorSpectrumShape.Ring
		};
		m_pickerView.ColorChanged += (_, _) => ApplyPickerColor();

		m_intensitySlider = new Slider {
			Minimum = 1.0,
			Maximum = IntensityMax,
			SmallChange = 0.1,
			LargeChange = 1.0
		};
		m_intensitySlider.PropertyChanged += (_, e) => {
			if (e.Property == RangeBase.ValueProperty) ApplyPickerColor();
		};

		var label = new TextBlock { Margin = new Thickness(0, 4, 0, 0) };
		label[!TextBlock.TextProperty] = new Binding("Value") {
			Source = m_intensitySlider,
			StringFormat = "HDR intensity: {0:0.0}x"
		};

		var content = new StackPanel { Margin = new Thickness(8), Spacing = 4 };
		content.Children.Add(m_pickerView);
		content.Children.Add(label);
		content.Children.Add(m_intensitySlider);

		m_pickerFlyout = new Flyout { Content = content, Placement = PlacementMode.Bottom };
	}

	private void ApplyPickerColor() {
		if (m_pickerSyncing || m_pickerView == null) return;

		var c = m_pickerView.Color;
		var intensity = (float)(m_intensitySlider?.Value ?? 1.0);
		R = (float)ClampChannel(0, c.R / 255f * intensity);
		G = (float)ClampChannel(1, c.G / 255f * intensity);
		B = (float)ClampChannel(2, c.B / 255f * intensity);
		if (HasAlpha) SetAlpha(c.A / 255f);
	}
}
