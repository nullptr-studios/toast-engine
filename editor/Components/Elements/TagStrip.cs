//
// TagStrip.cs
// 24 Sep 2026
//

using System;
using System.Collections.Generic;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.TextFormatting;
using Avalonia.Styling;
using editor.Assets;
using Lucide.Avalonia;

namespace editor.Components.Elements;

public sealed class TagStrip : Panel {
	public const string RemoveClass = "tag-remove";
	private const int MaxOverflowDots = 5;
	private const double DotSize = 6;

	public static readonly StyledProperty<IReadOnlyList<AssetTag>?> TagsProperty =
		AvaloniaProperty.Register<TagStrip, IReadOnlyList<AssetTag>?>(nameof(Tags));

	public static readonly StyledProperty<ICommand?> RemoveCommandProperty =
		AvaloniaProperty.Register<TagStrip, ICommand?>(nameof(RemoveCommand));

	public static readonly StyledProperty<bool> CanRemoveProperty =
		AvaloniaProperty.Register<TagStrip, bool>(nameof(CanRemove), true);

	public static readonly StyledProperty<double> SpacingProperty =
		AvaloniaProperty.Register<TagStrip, double>(nameof(Spacing), 4);

	private readonly List<Control> m_chips = [];
	private readonly Dictionary<int, double> m_overflowWidths = [];
	private readonly StackPanel m_overflowDots;
	private readonly TextBlock m_overflowText;
	private readonly Border m_overflow;
	private int m_overflowCount = -1;
	private int m_visibleCount;

	static TagStrip() {
		AffectsMeasure<TagStrip>(TagsProperty, SpacingProperty);
	}

	public TagStrip() {
		ClipToBounds = true;
		m_overflowDots = new StackPanel { Orientation = Orientation.Horizontal, Spacing = -3 };
		m_overflowText = MakeLabel();
		var content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 3 };
		content.Children.Add(m_overflowDots);
		content.Children.Add(m_overflowText);
		m_overflow = MakePill(content);
	}

	public IReadOnlyList<AssetTag>? Tags {
		get => GetValue(TagsProperty);
		set => SetValue(TagsProperty, value);
	}

	public ICommand? RemoveCommand {
		get => GetValue(RemoveCommandProperty);
		set => SetValue(RemoveCommandProperty, value);
	}

	public bool CanRemove {
		get => GetValue(CanRemoveProperty);
		set => SetValue(CanRemoveProperty, value);
	}

	public double Spacing {
		get => GetValue(SpacingProperty);
		set => SetValue(SpacingProperty, value);
	}

	public static int Fit(IReadOnlyList<double> widths, double available, double spacing,
		Func<int, double> overflowWidth) {
		var n = widths.Count;
		if (double.IsInfinity(available)) return n;

		var prefix = new double[n + 1];
		for (var i = 0; i < n; i++)
			prefix[i + 1] = prefix[i] + widths[i] + (i > 0 ? spacing : 0);

		for (var k = n; k > 0; k--) {
			var hidden = n - k;
			var total = prefix[k] + (hidden > 0 ? spacing + overflowWidth(hidden) : 0);
			if (total <= available + 0.5) return k;
		}

		return 0;
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == TagsProperty || change.Property == CanRemoveProperty ||
		    change.Property == RemoveCommandProperty)
			Rebuild();
	}

	protected override Size MeasureOverride(Size availableSize) {
		var tags = Tags;
		var n = m_chips.Count;
		if (tags is null || n == 0) {
			m_visibleCount = 0;
			return default;
		}

		var widths = new double[n];
		var height = 0.0;
		for (var i = 0; i < n; i++) {
			m_chips[i].Measure(Size.Infinity);
			widths[i] = m_chips[i].DesiredSize.Width;
			height = Math.Max(height, m_chips[i].DesiredSize.Height);
		}

		var spacing = Spacing;
		var visible = Fit(widths, availableSize.Width, spacing, OverflowWidth);
		m_visibleCount = visible;

		var hidden = n - visible;
		SetOverflow(tags, visible);
		m_overflow.Measure(Size.Infinity);

		var width = 0.0;
		for (var i = 0; i < visible; i++) width += widths[i] + (i > 0 ? spacing : 0);
		if (hidden > 0) {
			width += (visible > 0 ? spacing : 0) + m_overflow.DesiredSize.Width;
			height = Math.Max(height, m_overflow.DesiredSize.Height);
		}

		return new Size(width, height);
	}

	protected override Size ArrangeOverride(Size finalSize) {
		var x = 0.0;
		var spacing = Spacing;
		var parked = finalSize.Width + 10000;

		for (var i = 0; i < m_chips.Count; i++) {
			var chip = m_chips[i];
			var size = chip.DesiredSize;
			if (i < m_visibleCount) {
				chip.Arrange(new Rect(x, (finalSize.Height - size.Height) / 2, size.Width, size.Height));
				x += size.Width + spacing;
			} else {
				chip.Arrange(new Rect(parked, 0, size.Width, size.Height));
			}
		}

		var overflowSize = m_overflow.DesiredSize;
		var overflowX = m_visibleCount < m_chips.Count ? x : parked;
		m_overflow.Arrange(new Rect(overflowX, (finalSize.Height - overflowSize.Height) / 2,
			overflowSize.Width, overflowSize.Height));
		return finalSize;
	}

	private void Rebuild() {
		Children.Clear();
		m_chips.Clear();
		m_overflowWidths.Clear();
		m_overflowCount = -1;

		var tags = Tags;
		if (tags is null || tags.Count == 0) {
			ToolTip.SetTip(this, null);
			return;
		}

		var removable = CanRemove && RemoveCommand is not null;
		foreach (var tag in tags) {
			var chip = MakeChip(tag, removable);
			m_chips.Add(chip);
			Children.Add(chip);
		}

		Children.Add(m_overflow);

		var all = new WrapPanel { Orientation = Orientation.Horizontal, MaxWidth = 280, ItemSpacing = 4, LineSpacing = 4 };
		foreach (var tag in tags) all.Children.Add(MakeChip(tag, false, true));
		ToolTip.SetTip(this, all);
	}

	private double OverflowWidth(int count) {
		if (m_overflowWidths.TryGetValue(count, out var cached)) return cached;

		var dots = Math.Min(count, MaxOverflowDots);
		var dotsWidth = DotSize * dots + m_overflowDots.Spacing * (dots - 1);
		var typeface = new Typeface(m_overflowText.FontFamily, m_overflowText.FontStyle, m_overflowText.FontWeight);
		using var layout = new TextLayout($"+{count}", typeface, m_overflowText.FontSize, null);
		var padding = m_overflow.Padding.Left + m_overflow.Padding.Right;
		var width = padding + dotsWidth + 3 + layout.Width;
		return m_overflowWidths[count] = Math.Ceiling(width);
	}

	private void SetOverflow(IReadOnlyList<AssetTag> tags, int firstHidden) {
		var count = tags.Count - firstHidden;
		if (count == m_overflowCount) return;
		m_overflowCount = count;

		m_overflowDots.Children.Clear();
		for (var i = 0; i < Math.Min(count, MaxOverflowDots); i++)
			m_overflowDots.Children.Add(MakeDot(tags[firstHidden + i]));
		m_overflowText.Text = $"+{count}";
	}

	private Control MakeChip(AssetTag tag, bool removable, bool large = false) {
		var dot = MakeDot(tag, large);
		var cell = new Panel { Width = DotSize, Height = DotSize, VerticalAlignment = VerticalAlignment.Center };
		cell.Children.Add(dot);

		var label = MakeLabel(large);
		label[!TextBlock.TextProperty] = new Binding(nameof(AssetTag.Name)) { Source = tag };

		var content = new StackPanel { Orientation = Orientation.Horizontal, Spacing = large ? 4 : 3 };
		content.Children.Add(cell);
		content.Children.Add(label);
		var chip = MakePill(content, true);
		if (!removable) return chip;

		var remove = new Border {
			Margin = new Thickness(-1.5),
			IsVisible = false,
			Cursor = new Cursor(StandardCursorType.Hand),
			Background = Brushes.Transparent,
			Child = new LucideIcon {
				Kind = LucideIconKind.X,
				Size = 9,
				StrokeWidth = 3.5,
				Foreground = Resource("Text") ?? Brushes.White
			}
		};
		remove.Classes.Add(RemoveClass);
		ToolTip.SetTip(remove, "Remove tag");
		remove.PointerPressed += (_, e) => {
			if (!e.GetCurrentPoint(remove).Properties.IsLeftButtonPressed) return;
			e.Handled = true;
			var command = RemoveCommand;
			if (command?.CanExecute(tag) == true) command.Execute(tag);
		};
		cell.Children.Add(remove);

		chip.PointerEntered += (_, _) => {
			dot.IsVisible = false;
			remove.IsVisible = true;
		};
		chip.PointerExited += (_, _) => {
			dot.IsVisible = true;
			remove.IsVisible = false;
		};
		return chip;
	}

	private static Border MakeDot(AssetTag tag, bool large = false) {
		var dot = new Border {
			Width = large ? DotSize * 1.5f : DotSize,
			Height = large ? DotSize * 1.5f : DotSize,
			CornerRadius = new CornerRadius(100),
			VerticalAlignment = VerticalAlignment.Center
		};
		dot[!Border.BackgroundProperty] = new Binding(nameof(AssetTag.Brush)) { Source = tag };
		return dot;
	}

	private static TextBlock MakeLabel(bool large = false) {
		return new TextBlock {
			FontSize = large ? 12 : 10,
			FontWeight = FontWeight.SemiBold,
			Foreground = Resource("TextMuted") ?? Brushes.Gray,
			VerticalAlignment = VerticalAlignment.Center
		};
	}

	private static Border MakePill(Control content, bool large = false) {
		return new Border {
			Background = Resource("Bg4") ?? Brushes.DimGray,
			Padding = new Thickness(large ? 6 : 4, large ? 2 : 1),
			CornerRadius = new CornerRadius(100),
			Child = content
		};
	}

	private static IBrush? Resource(string key) {
		if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true)
			return r as IBrush;
		return null;
	}
}
