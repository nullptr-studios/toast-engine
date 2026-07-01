using Avalonia.Controls;
using Avalonia.Interactivity;

namespace editor.Components.Modals;

public partial class SaveFileModal : Window {
	public SaveFileModal() {
		InitializeComponent();
		DataContext = new SaveFileViewModel();
	}

	public SaveFileModal(string defaultName, string extension = ".tnode") {
		InitializeComponent();
		var vm = new SaveFileViewModel(defaultName, extension);
		DataContext = vm;
	}

	private void Name_OnTextChanged(object? sender, TextChangedEventArgs e) {
		// Name updates are handled by binding
	}

	private async void Folder_OnClick(object? sender, RoutedEventArgs e) {
		var result = await new AssetFolderTree().ShowDialog<string?>(this);
		if (result is { })
			((SaveFileViewModel)DataContext!).SetFolder(result);
	}

	private void Save_OnClick(object? sender, RoutedEventArgs e) {
		Close(((SaveFileViewModel)DataContext!).Result());
	}

	private void Cancel_OnClick(object? sender, RoutedEventArgs e) {
		Close(null);
	}
}
