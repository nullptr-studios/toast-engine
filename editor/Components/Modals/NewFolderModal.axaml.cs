using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace editor.Components.Modals;

public partial class NewFolderModal : Window {
	public NewFolderModal() {
		InitializeComponent();
	}

	private void OnCreate(object? sender, RoutedEventArgs e) {
		var name = FolderNameBox.Text?.Trim();
		if (!string.IsNullOrEmpty(name)) Close(name);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}

	private void OnKeyDown(object? sender, KeyEventArgs e) {
		if (e.Key == Key.Enter) {
			var name = FolderNameBox.Text?.Trim();
			if (!string.IsNullOrEmpty(name)) Close(name);
		} else if (e.Key == Key.Escape) {
			Close(null);
		}
	}
}
