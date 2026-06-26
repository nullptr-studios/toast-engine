//
// BoolBox.cs by Xein
// 24 Jun 2026
//

using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Data;
using Avalonia.Input;

namespace editor.Components.Elements;

public sealed class BoolBox : TemplatedControl {
	public static readonly StyledProperty<bool> ValueProperty =
		AvaloniaProperty.Register<BoolBox, bool>(nameof(Value), defaultBindingMode: BindingMode.TwoWay);

	private string m_displayText = "false";

	public static readonly DirectProperty<BoolBox, string> DisplayTextProperty =
		AvaloniaProperty.RegisterDirect<BoolBox, string>(nameof(DisplayText), o => o.m_displayText);

	private Control? m_root;

	public bool Value {
		get => GetValue(ValueProperty);
		set => SetValue(ValueProperty, value);
	}

	public string DisplayText => m_displayText;

	protected override Type StyleKeyOverride => typeof(BoolBox);

	protected override void OnApplyTemplate(TemplateAppliedEventArgs e) {
		base.OnApplyTemplate(e);
		DetachHandlers();

		m_root = e.NameScope.Find<Control>("PART_Root");
		if (m_root != null) m_root.Tapped += OnRootTapped;

		RebuildDisplay();
	}

	private void DetachHandlers() {
		if (m_root != null) m_root.Tapped -= OnRootTapped;
	}

	private void OnRootTapped(object? sender, TappedEventArgs e) {
		if (!IsEnabled) return;
		Value = !Value;
		e.Handled = true;
	}

	private void RebuildDisplay() {
		SetAndRaise(DisplayTextProperty, ref m_displayText, Value ? "true" : "false");
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == ValueProperty) RebuildDisplay();
	}
}
