using System;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Lucide.Avalonia;

namespace editor.Components.Modals;

public partial class RenameModal : Window {
	public RenameModal(
		string initialName,
		string title = "Rename",
		string okLabel = "Rename",
		LucideIconKind okIcon = LucideIconKind.Pencil,
		string? placeholder = null) {
		InitializeComponent();
		NameBox.Text = initialName;
		if (placeholder is not null) NameBox.PlaceholderText = placeholder;
		Title = title;
		OkText.Text = okLabel;
		OkIcon.Kind = okIcon;
	}

	protected override void OnOpened(EventArgs e) {
		base.OnOpened(e);
		NameBox.SelectAll();
		NameBox.Focus();
	}

	private void OnRename(object? sender, RoutedEventArgs e) {
		var name = NameBox.Text?.Trim();
		if (!string.IsNullOrEmpty(name)) Close(name);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}
}
