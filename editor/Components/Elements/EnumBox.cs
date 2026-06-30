//
// EnumBox.cs by Xein
// 28 Jun 2026
//

using System;
using System.Collections.Generic;
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

    private bool m_isOpen;
    public static readonly DirectProperty<EnumBox, bool> IsOpenProperty =
        AvaloniaProperty.RegisterDirect<EnumBox, bool>(nameof(IsOpen), o => o.m_isOpen);

    private Popup?       m_popup;
    private ItemsControl? m_list;

    public string? Value {
        get => GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }

    public IEnumerable<string>? Options {
        get => GetValue(OptionsProperty);
        set => SetValue(OptionsProperty, value);
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
        m_list  = e.NameScope.Find<ItemsControl>("PART_List");
        var root = e.NameScope.Find<Control>("PART_Root");

        if (root   is not null) root.Tapped += OnRootTapped;
        if (m_popup is not null) m_popup.Closed += OnPopupClosed;
        if (m_list  is not null) {
            m_list.ItemTemplate = new FuncDataTemplate<string>((opt, _) => BuildOption(opt), false);
            m_list.ItemsSource  = Options;
        }
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
        base.OnPropertyChanged(change);
        if (change.Property == OptionsProperty && m_list is not null)
            m_list.ItemsSource = Options;
    }

    private void OnRootTapped(object? sender, TappedEventArgs e) {
        if (!IsEnabled) return;
        ToggleOpen();
        e.Handled = true;
    }

    private void OnPopupClosed(object? sender, EventArgs e) => IsOpen = false;

    private void ToggleOpen() {
        IsOpen = !IsOpen;
        if (m_popup is not null) m_popup.IsOpen = IsOpen;
    }

    private void Select(string option) {
        Value  = option;
        IsOpen = false;
        if (m_popup is not null) m_popup.IsOpen = false;
    }

    private Control BuildOption(string option) {
        var selected = string.Equals(option, Value, StringComparison.Ordinal);
        var btn = new Button {
            Content                  = option,
            HorizontalAlignment      = HorizontalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Left,
            Background               = selected ? Brush("Red") : Brushes.Transparent,
            Foreground               = Brush(selected ? "Bg1" : "Text"),
            BorderThickness          = new Thickness(0),
            Padding                  = new Thickness(8, 6),
            CornerRadius             = new CornerRadius(4),
            Cursor                   = new Cursor(StandardCursorType.Hand),
            FontSize                 = 15,
        };
        btn.Click += (_, _) => Select(option);
        return btn;
    }

    private static IBrush? Brush(string key) {
        if (Application.Current?.Resources.TryGetResource(key, ThemeVariant.Dark, out var r) == true)
            return r as IBrush;
        return null;
    }
}
