using System;
using Avalonia.Controls;
using Avalonia.Interactivity;

namespace editor.Components.Modals;

public partial class NewFolderModal : Window {
	public NewFolderModal() {
		InitializeComponent();
	}

	protected override void OnOpened(EventArgs e) {
		base.OnOpened(e);
		FolderNameBox.SelectAll();
		FolderNameBox.Focus();
	}

	private void OnCreate(object? sender, RoutedEventArgs e) {
		var name = FolderNameBox.Text?.Trim();
		if (!string.IsNullOrEmpty(name)) Close(name);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}
}
