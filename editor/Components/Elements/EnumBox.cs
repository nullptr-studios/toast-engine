//
// EnumBox.cs by Xein
// 28 Jun 2026
//

using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Threading;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Controls.Templates;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Styling;

namespace editor.Components.Elements;

public sealed class EnumBox : TemplatedControl {
	public static readonly StyledProperty<string?> ValueProperty =
		AvaloniaProperty.Register<EnumBox, string?>(nameof(Value), defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<IEnumerable<string>?> OptionsProperty =
		AvaloniaProperty.Register<EnumBox, IEnumerable<string>?>(nameof(Options));

	public static readonly DirectProperty<EnumBox, bool> IsOpenProperty =
		AvaloniaProperty.RegisterDirect<EnumBox, bool>(nameof(IsOpen), o => o.m_isOpen);

	public static readonly StyledProperty<IReadOnlyDictionary<string, string>?> GroupsProperty =
		AvaloniaProperty.Register<EnumBox, IReadOnlyDictionary<string, string>?>(nameof(Groups));

	public const int SearchThreshold = 12;

	private sealed record Header(string Text, bool Folded, int Count);

	private readonly HashSet<string> m_folded = [];

	private bool m_isOpen;
	private ItemsControl? m_list;
	private TextBox? m_search;
	private List<string> m_visible = [];
	private int m_highlight = -1;
	private Button? m_highlighted;

	private Popup? m_popup;

	public string? Value {
		get => GetValue(ValueProperty);
		set => SetValue(ValueProperty, value);
	}

	public IEnumerable<string>? Options {
		get => GetValue(OptionsProperty);
		set => SetValue(OptionsProperty, value);
	}

	public IReadOnlyDictionary<string, string>? Groups {
		get => GetValue(GroupsProperty);
		set => SetValue(GroupsProperty, value);
	}

	public bool IsOpen {
		get => m_isOpen;
		private set => SetAndRaise(IsOpenProperty, ref m_isOpen, value);
	}

	protected override Type StyleKeyOverride => typeof(EnumBox);

	protected override void OnApplyTemplate(TemplateAppliedEventArgs e) {
		base.OnApplyTemplate(e);

		if (m_popup is not null) m_popup.Closed -= OnPopupClosed;

		m_popup = e.NameScope.Find<Popup>("PART_Popup");
		m_list = e.NameScope.Find<ItemsControl>("PART_List");
		var root = e.NameScope.Find<Control>("PART_Root");

		if (root is not null) root.Tapped += OnRootTapped;
		if (m_popup is not null) m_popup.Closed += OnPopupClosed;
		if (m_search is not null) {
			m_search.TextChanged -= OnSearchChanged;
			m_search.KeyDown -= OnSearchKey;
		}
		m_search = e.NameScope.Find<TextBox>("PART_Search");
		if (m_search is not null) {
			m_search.TextChanged += OnSearchChanged;
			m_search.KeyDown += OnSearchKey;
		}
		if (m_list is not null) {
			m_list.ItemTemplate = new FuncDataTemplate<object>((item, _) => item is Header header ? BuildHeader(header) : BuildOption((string)item));
			Rebuild();
		}

	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if ((change.Property == OptionsProperty || change.Property == GroupsProperty) && m_list is not null) Rebuild();
	}

	private void OnRootTapped(object? sender, TappedEventArgs e) {
		if (!IsEnabled) return;
		ToggleOpen();
		e.Handled = true;
	}

	private void OnPopupClosed(object? sender, EventArgs e) {
		IsOpen = false;
	}

	private void ToggleOpen() {
		IsOpen = !IsOpen;
		if (m_popup is not null) m_popup.IsOpen = IsOpen;
		if (!IsOpen) return;
		if (m_search is not null) {
			m_search.Text = "";
			m_search.IsVisible = (Options?.Count() ?? 0) > SearchThreshold;
		}
		m_folded.Clear();
		if (Groups is { } groupNames) {
			var first = groupNames.Values.FirstOrDefault();
			var current = Value is { } value && groupNames.TryGetValue(value, out var own) ? own : null;
			foreach (var name in groupNames.Values.Distinct())
				if (name != first && name != current) m_folded.Add(name);
		}
		Rebuild(highlightValue: true);
		if (m_search is { IsVisible: true }) Dispatcher.UIThread.Post(() => m_search.Focus());
	}

	private void OnSearchChanged(object? sender, TextChangedEventArgs e) => Rebuild();

	private void OnSearchKey(object? sender, KeyEventArgs e) {
		switch (e.Key) {
			case Key.Down:
				m_highlight = Math.Min(m_highlight + 1, m_visible.Count - 1);
				Rebuild(keepHighlight: true);
				break;
			case Key.Up:
				m_highlight = Math.Max(m_highlight - 1, 0);
				Rebuild(keepHighlight: true);
				break;
			case Key.Enter:
				if (m_highlight >= 0 && m_highlight < m_visible.Count) Select(m_visible[m_highlight]);
				break;
			case Key.Escape:
				IsOpen = false;
				if (m_popup is not null) m_popup.IsOpen = false;
				break;
			default:
				return;
		}
		e.Handled = true;
	}

	private void Rebuild(bool highlightValue = false, bool keepHighlight = false) {
		if (m_list is null) return;
		var all = Options?.ToList() ?? [];
		var query = m_search?.Text?.Trim() ?? "";
		var matching = all.Where(o => query.Length == 0 || o.Contains(query, StringComparison.OrdinalIgnoreCase)).ToList();
		var items = new List<object>();
		if (Groups is { } groups) {
			var searching = query.Length > 0;
			foreach (var group in matching.GroupBy(o => groups.GetValueOrDefault(o, "Other"))) {
				var folded = !searching && m_folded.Contains(group.Key);
				items.Add(new Header(group.Key, folded, group.Count()));
				if (!folded) items.AddRange(group);
			}
			m_visible = items.OfType<string>().ToList();
		} else {
			m_visible = matching;
			items.AddRange(matching);
		}
		if (!keepHighlight) m_highlight = highlightValue && Value is { } v && m_visible.IndexOf(v) is var at and >= 0 ? at : m_visible.Count > 0 ? 0 : -1;
		m_highlighted = null;
		m_list.ItemsSource = items;
		Dispatcher.UIThread.Post(() => m_highlighted?.BringIntoView(), DispatcherPriority.Loaded);
	}

	private Control BuildHeader(Header header) {
		var line = new Button {
			Content = new TextBlock {
				Text = (header.Folded ? "▸  " : "▾  ") + header.Text + "   " + header.Count,
				FontSize = 13,
				FontWeight = FontWeight.Bold,
				Foreground = Brush("Text")
			},
			HorizontalAlignment = HorizontalAlignment.Stretch,
			HorizontalContentAlignment = HorizontalAlignment.Left,
			Background = Brush("Bg4"),
			BorderThickness = new Thickness(0),
			Padding = new Thickness(8, 5),
			Margin = new Thickness(0, 4, 0, 0),
			CornerRadius = new CornerRadius(4),
			Cursor = new Cursor(StandardCursorType.Hand)
		};
		line.Click += (_, _) => {
			if (!m_folded.Remove(header.Text)) m_folded.Add(header.Text);
			Rebuild(keepHighlight: true);
		};
		return line;
	}

	private void Select(string option) {
		Value = option;
		IsOpen = false;
		if (m_popup is not null) m_popup.IsOpen = false;
	}

	private Control BuildOption(string option) {
		var selected = string.Equals(option, Value, StringComparison.Ordinal);
		var highlighted = m_highlight >= 0 && m_highlight < m_visible.Count && m_visible[m_highlight] == option;
		var btn = new Button {
			Content = option,
			HorizontalAlignment = HorizontalAlignment.Stretch,
			HorizontalContentAlignment = HorizontalAlignment.Left,
			Background = selected ? Brush("Red") : highlighted ? Brush("Bg4") : Brushes.Transparent,
			Foreground = Brush(selected ? "Bg1" : "Text"),
			BorderThickness = new Thickness(0),
			Padding = new Thickness(8, 6),
			CornerRadius = new CornerRadius(4),
			Cursor = new Cursor(StandardCursorType.Hand),
			FontSize = 15
		};
		btn.Click += (_, _) => Select(option);
		if (highlighted) m_highlighted = btn;
		return btn;
	}

	private static IBrush? Brush(string key) {
		if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true)
			return r as IBrush;
		return null;
	}
}
