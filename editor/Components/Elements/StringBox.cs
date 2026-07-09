//
// StringBox.cs by Xein
// 24 Jun 2026
//

using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Threading;

namespace editor.Components.Elements;

public sealed class StringBox : TemplatedControl {
	public static readonly StyledProperty<string?> ValueProperty =
		AvaloniaProperty.Register<StringBox, string?>(nameof(Value), "", defaultBindingMode: BindingMode.TwoWay);

	public static readonly StyledProperty<bool> WrapProperty =
		AvaloniaProperty.Register<StringBox, bool>(nameof(Wrap), true);

	public static readonly DirectProperty<StringBox, TextWrapping> WrapModeProperty =
		AvaloniaProperty.RegisterDirect<StringBox, TextWrapping>(nameof(WrapMode), o => o.m_wrapMode);

	public static readonly DirectProperty<StringBox, bool> IsEditingProperty =
		AvaloniaProperty.RegisterDirect<StringBox, bool>(nameof(IsEditing), o => o.m_isEditing);

	private TextBox? m_editor;

	private bool m_isEditing;

	private Control? m_root;

	private TextWrapping m_wrapMode = TextWrapping.Wrap;

	public string? Value {
		get => GetValue(ValueProperty);
		set => SetValue(ValueProperty, value);
	}

	public bool Wrap {
		get => GetValue(WrapProperty);
		set => SetValue(WrapProperty, value);
	}

	public TextWrapping WrapMode => m_wrapMode;

	public bool IsEditing {
		get => m_isEditing;
		private set => SetAndRaise(IsEditingProperty, ref m_isEditing, value);
	}

	protected override Type StyleKeyOverride => typeof(StringBox);

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == WrapProperty)
			SetAndRaise(WrapModeProperty, ref m_wrapMode, Wrap ? TextWrapping.Wrap : TextWrapping.NoWrap);
	}

	protected override void OnApplyTemplate(TemplateAppliedEventArgs e) {
		base.OnApplyTemplate(e);
		DetachHandlers();

		m_root = e.NameScope.Find<Control>("PART_Root");
		m_editor = e.NameScope.Find<TextBox>("PART_Editor");

		if (m_root != null) m_root.Tapped += OnRootTapped;

		if (m_editor != null) {
			m_editor.KeyDown += OnEditorKeyDown;
			m_editor.LostFocus += OnEditorLostFocus;
		}
	}

	private void DetachHandlers() {
		if (m_root != null) m_root.Tapped -= OnRootTapped;

		if (m_editor != null) {
			m_editor.KeyDown -= OnEditorKeyDown;
			m_editor.LostFocus -= OnEditorLostFocus;
		}
	}

	private void OnRootTapped(object? sender, TappedEventArgs e) {
		if (!IsEnabled || IsEditing) return;
		BeginEdit();
		e.Handled = true;
	}

	private void BeginEdit() {
		if (!IsEnabled || m_editor == null) return;
		IsEditing = true;
		m_editor.Text = Value ?? "";
		Dispatcher.UIThread.Post(() => {
			m_editor.Focus();
			m_editor.SelectAll();
		});
	}

	private void CommitEdit() {
		if (!IsEditing) return;
		if (m_editor != null) Value = m_editor.Text ?? "";
		IsEditing = false;
	}

	private void CancelEdit() {
		if (!IsEditing) return;
		IsEditing = false;
	}

	private void OnEditorKeyDown(object? sender, KeyEventArgs e) {
		if (e.Key == Key.Escape) {
			CancelEdit();
			e.Handled = true;
		} else if (e.Key == Key.Enter) {
			CommitEdit();
			e.Handled = true;
		}
	}

	private void OnEditorLostFocus(object? sender, RoutedEventArgs e) {
		CommitEdit();
	}
}
