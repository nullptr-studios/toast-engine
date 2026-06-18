using Avalonia.Controls;
using Avalonia.Interactivity;

namespace editor.Components.Modals;

public partial class AssetTreePickerWindow : Window {
	public AssetTreePickerWindow() {
		InitializeComponent();
		DataContext = new AssetTreePickerViewModel();
	}

	private void OnConfirm(object? sender, RoutedEventArgs e) {
		Close(((AssetTreePickerViewModel)DataContext!).ResultPath);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}
}
