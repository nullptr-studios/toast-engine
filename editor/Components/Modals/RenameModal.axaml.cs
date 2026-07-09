using System;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace editor.Components.Modals;

public partial class RenameModal : Window {
	public RenameModal(string initialName) {
		InitializeComponent();
		NameBox.Text = initialName;
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

	private void OnKeyDown(object? sender, KeyEventArgs e) {
		if (e.Key == Key.Enter) {
			var name = NameBox.Text?.Trim();
			if (!string.IsNullOrEmpty(name)) Close(name);
		} else if (e.Key == Key.Escape) {
			Close(null);
		}
	}
}
