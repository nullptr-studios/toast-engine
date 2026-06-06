using Avalonia.Controls;
using Avalonia.Interactivity;

namespace editor.Import;

public partial class AssetFolderPickerDialog : Window {
	public AssetFolderPickerDialog() {
		InitializeComponent();
		DataContext = new AssetFolderPickerViewModel();
	}

	private void OnConfirm(object? sender, RoutedEventArgs e) {
		Close(((AssetFolderPickerViewModel)DataContext!).ResultPath);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}
}
