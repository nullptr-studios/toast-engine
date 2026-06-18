using Avalonia.Controls;
using Avalonia.Interactivity;

namespace editor.Components.Modals;

public partial class SaveFileModal : Window {
	public SaveFileModal() {
		InitializeComponent();
		DataContext = new SaveFileViewModel();
	}

	public SaveFileModal(string defaultName) {
		InitializeComponent();
		DataContext = new SaveFileViewModel(defaultName);
	}

	private async void OnBrowse(object? sender, RoutedEventArgs e) {
		var vm = (SaveFileViewModel)DataContext!;
		if (await AssetTreePicker.PickFolder(this) is { } folder)
			vm.SetFolder(folder);
	}

	private void OnConfirm(object? sender, RoutedEventArgs e) {
		Close(((SaveFileViewModel)DataContext!).Result());
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}
}
