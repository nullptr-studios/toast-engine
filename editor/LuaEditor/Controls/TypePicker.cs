using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;
using Style = Avalonia.Styling.Style;
using Avalonia.Threading;
using Lucide.Avalonia;
using editor.LuaEditor.Editing;

namespace editor.LuaEditor.Controls;

public sealed record PickItem(string Value, string Group, LucideIconKind Icon, string Color, PinShape? Glyph = null);

public sealed class PickRow {
	public string Text { get; init; } = "";
	public string Count { get; init; } = "";
	public bool IsGroup { get; init; }
	public bool IsOpen { get; init; }
	public int Depth { get; init; }
	public string Key { get; init; } = "";
	public PickItem? Item { get; init; }

	public static List<PickRow> Tree(IReadOnlyList<PickItem> items, ISet<string> open, bool everythingOpen) {
		var rows = new List<PickRow>();
		foreach (var group in items.GroupBy(i => i.Group)) {
			var isOpen = everythingOpen || open.Contains(group.Key);
			rows.Add(new PickRow { Text = group.Key, Count = group.Count().ToString(), IsGroup = true, IsOpen = isOpen, Key = group.Key });
			if (isOpen) rows.AddRange(group.Select(i => new PickRow { Text = i.Value, Depth = 1, Item = i, Key = i.Value }));
		}
		return rows;
	}
}

public sealed class TypePicker : UserControl {
	public static readonly StyledProperty<string> ValueProperty = AvaloniaProperty.Register<TypePicker, string>(nameof(Value), "", defaultBindingMode: Avalonia.Data.BindingMode.TwoWay);
	public static readonly StyledProperty<IReadOnlyList<PickItem>?> ItemsProperty = AvaloniaProperty.Register<TypePicker, IReadOnlyList<PickItem>?>(nameof(Items));

	private readonly Button m_button = new();
	private readonly StackPanel m_face = new() { Orientation = Orientation.Horizontal, Spacing = 8 };
	private readonly Popup m_popup = new();
	private readonly TextBox m_search = new() { PlaceholderText = "Search...", MinHeight = 0, Padding = new Thickness(6, 3), FontSize = 13 };
	private readonly ListBox m_list = new();
	private readonly HashSet<string> m_open = [];

	static TypePicker() {
		ValueProperty.Changed.AddClassHandler<TypePicker>((p, _) => p.ShowFace());
		ItemsProperty.Changed.AddClassHandler<TypePicker>((p, _) => p.ShowFace());
	}

	public TypePicker() {
		m_button.HorizontalAlignment = HorizontalAlignment.Stretch;
		m_button.HorizontalContentAlignment = HorizontalAlignment.Left;
		m_button.CornerRadius = new CornerRadius(6);
		m_button.Padding = new Thickness(6, 4);
		m_button.MinHeight = 26;
		m_button.FontSize = 13;
		m_button.FontFamily = new FontFamily("FiraCode Nerd Font,Consolas,monospace");
		m_button.VerticalContentAlignment = VerticalAlignment.Center;
		m_button.Background = Brush("Bg1");
		m_button.BorderThickness = new Thickness(0);
		m_button.Content = m_face;
		m_button.Click += (_, _) => Open();

		m_list.ItemTemplate = new Avalonia.Controls.Templates.FuncDataTemplate<PickRow>((row, _) => Build(row));
		m_list.Background = Brushes.Transparent;
		m_list.Padding = new Thickness(0);
		m_list.Styles.Add(new Style(x => x.OfType<ListBoxItem>()) {
			Setters = {
				new Setter(ListBoxItem.PaddingProperty, new Thickness(6, 0)),
				new Setter(ListBoxItem.MinHeightProperty, 0d),
				new Setter(ListBoxItem.HeightProperty, 22d),
				new Setter(ListBoxItem.CornerRadiusProperty, new CornerRadius(4))
			}
		});
		m_list.Tapped += (_, _) => ActivateSelection();
		m_search.TextChanged += (_, _) => Fill();
		m_search.KeyDown += OnKey;

		var card = new Border {
			Background = Brush("Bg3"),
			CornerRadius = new CornerRadius(8),
			Padding = new Thickness(6),
			BorderBrush = Brush("Bg4"),
			BorderThickness = new Thickness(1),
			Child = new StackPanel {
				Spacing = 6,
				Children = { m_search, new ScrollViewer { MaxHeight = 240, HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled, Content = m_list } }
			}
		};
		m_popup.Child = card;
		m_popup.IsLightDismissEnabled = true;
		m_popup.Placement = PlacementMode.Bottom;
		m_popup.PlacementTarget = m_button;
		m_popup.MinWidth = 220;
		m_popup.MaxWidth = 300;

		Content = new Panel { Children = { m_button, m_popup } };
	}

	public string Value {
		get => GetValue(ValueProperty);
		set => SetValue(ValueProperty, value);
	}

	public IReadOnlyList<PickItem>? Items {
		get => GetValue(ItemsProperty);
		set => SetValue(ItemsProperty, value);
	}

	private static IBrush? Brush(string key) {
		if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true) return r as IBrush;
		return null;
	}

	private PickItem? Current => Items?.FirstOrDefault(i => i.Value == Value);

	private static Control IconOf(PickItem? item, string fallbackColor = "TextMuted") {
		if (item?.Glyph is { } shape) return new PinShapeIcon { Shape = shape, Color = item.Color.StartsWith('#') ? item.Color : "#d8d8d8", Width = 14, Height = 14 };
		return new LucideIcon { Kind = item?.Icon ?? LucideIconKind.CircleDot, Size = 13, StrokeWidth = 2.5, Foreground = Brush(item?.Color ?? fallbackColor) };
	}

	private void ShowFace() {
		m_face.Children.Clear();
		var current = Current;
		m_face.Children.Add(IconOf(current));
		m_face.Children.Add(new TextBlock { Text = Value.Length == 0 ? "(none)" : Value, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis });
		m_face.Children.Add(new LucideIcon { Kind = LucideIconKind.ChevronDown, Size = 14, StrokeWidth = 2, Foreground = Brush("TextMuted"), VerticalAlignment = VerticalAlignment.Center });
	}

	private void Open() {
		m_open.Clear();
		var first = Items?.FirstOrDefault()?.Group;
		if (first is not null) m_open.Add(first);
		if (Current is { } current) m_open.Add(current.Group);
		m_search.Text = "";
		Fill(Value);
		m_popup.IsOpen = true;
		Dispatcher.UIThread.Post(() => m_search.Focus());
	}

	private void Fill(string? keep = null) {
		var items = Items ?? [];
		var query = (m_search.Text ?? "").Trim();
		var shown = query.Length == 0 ? items : items.Where(i => i.Value.Contains(query, StringComparison.OrdinalIgnoreCase)).ToList();
		var rows = PickRow.Tree(shown, m_open, query.Length > 0);
		m_list.ItemsSource = rows;
		var at = keep is null ? -1 : rows.FindIndex(r => r.Item is not null && r.Key == keep);
		m_list.SelectedIndex = at >= 0 ? at : rows.FindIndex(r => r.Item is not null) is var first and >= 0 && query.Length > 0 ? first : 0;
		Reveal();
	}

	private Control Build(PickRow? row) {
		if (row is null) return new Panel();
		var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,Auto,*,Auto"), Height = 22, Margin = new Thickness(row.Depth * 14, 0, 0, 0) };
		if (row.IsGroup) {
			grid.Children.Add(new TextBlock { Text = row.IsOpen ? "▾" : "▸", FontSize = 11, Foreground = Brush("TextMuted"), Margin = new Thickness(0, 0, 6, 0), VerticalAlignment = VerticalAlignment.Center });
		} else {
			var icon = IconOf(row.Item);
			Grid.SetColumn(icon, 1);
			icon.Margin = new Thickness(0, 0, 6, 0);
			grid.Children.Add(icon);
		}
		var text = new TextBlock { Text = row.Text, FontSize = 12, VerticalAlignment = VerticalAlignment.Center, FontWeight = row.IsGroup ? FontWeight.Bold : FontWeight.Normal, TextTrimming = TextTrimming.CharacterEllipsis };
		Grid.SetColumn(text, 2);
		grid.Children.Add(text);
		if (row.Count.Length > 0) {
			var count = new TextBlock { Text = row.Count, Foreground = Brush("TextMuted"), FontSize = 11, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(8, 0, 4, 0) };
			Grid.SetColumn(count, 3);
			grid.Children.Add(count);
		}
		return grid;
	}

	private void Reveal() {
		Dispatcher.UIThread.Post(() => {
			try {
				if (m_list.SelectedItem is { } selected) m_list.ScrollIntoView(selected);
			} catch (Exception) {
			}
		}, DispatcherPriority.Loaded);
	}

	private void ActivateSelection() {
		if (m_list.SelectedItem is not PickRow row) return;
		if (row.IsGroup) Toggle(row);
		else Choose(row);
	}

	private void Toggle(PickRow row, bool? open = null) {
		if ((m_search.Text ?? "").Trim().Length > 0) return;
		if (open ?? !m_open.Contains(row.Key)) m_open.Add(row.Key);
		else m_open.Remove(row.Key);
		Fill();
		var at = (m_list.ItemsSource as List<PickRow>)?.FindIndex(r => r.IsGroup && r.Key == row.Key) ?? -1;
		if (at >= 0) m_list.SelectedIndex = at;
	}

	private void Choose(PickRow row) {
		if (row.Item is null) return;
		Value = row.Item.Value;
		m_popup.IsOpen = false;
	}

	private void OnKey(object? sender, KeyEventArgs e) {
		switch (e.Key) {
			case Key.Down:
				m_list.SelectedIndex = Math.Min(m_list.SelectedIndex + 1, m_list.ItemCount - 1);
				Reveal();
				break;
			case Key.Up:
				m_list.SelectedIndex = Math.Max(m_list.SelectedIndex - 1, 0);
				Reveal();
				break;
			case Key.Right when m_list.SelectedItem is PickRow { IsGroup: true } closed:
				Toggle(closed, true);
				break;
			case Key.Left when m_list.SelectedItem is PickRow { IsGroup: true } open:
				Toggle(open, false);
				break;
			case Key.Enter:
				ActivateSelection();
				break;
			case Key.Escape:
				m_popup.IsOpen = false;
				break;
			default:
				return;
		}
		e.Handled = true;
	}
}
