using Avalonia.Controls;
using Avalonia.Interactivity;
using editor.Import;

namespace editor.Workspace;

public partial class SaveWorkspaceDialog : Window {
	public SaveWorkspaceDialog() {
		InitializeComponent();
		DataContext = new SaveWorkspaceViewModel();
	}

	public SaveWorkspaceDialog(string defaultName) {
		InitializeComponent();
		DataContext = new SaveWorkspaceViewModel(defaultName);
	}

	private async void OnBrowse(object? sender, RoutedEventArgs e) {
		var vm = (SaveWorkspaceViewModel)DataContext!;
		var picker = new AssetFolderPickerDialog();
		if (await picker.ShowDialog<string?>(this) is { } folder) {
			vm.SetFolder(folder);
		}
	}

	private void OnConfirm(object? sender, RoutedEventArgs e) {
		Close(((SaveWorkspaceViewModel)DataContext!).Result());
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}
}
