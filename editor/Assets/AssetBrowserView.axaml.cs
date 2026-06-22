using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using editor.Assets.Importers;

namespace editor.Assets;

public partial class AssetBrowserView : UserControl {
	public AssetBrowserView() {
		InitializeComponent();
	}

	private AssetBrowserViewModel Vm => (AssetBrowserViewModel)DataContext!;

	private void OnSelectionChanged(object? sender, SelectionChangedEventArgs e) {
		if (sender is ListBox lb)
			Vm.UpdateSelection(lb.SelectedItems);
	}

	private void OnFolderDoubleTapped(object? sender, TappedEventArgs e) {
		if (sender is Border { DataContext: AssetFolder folder }) {
			Vm.SearchText = "";
			Vm.SelectedFolder = folder;
			e.Handled = true;
		}
	}

	private async void Import_OnClick(object? sender, RoutedEventArgs e) {
		var owner = TopLevel.GetTopLevel(this) as Window;
		if (owner is null) return;
		var importWindow = new ImportWindow();
		await importWindow.ShowDialog(owner);
	}
}
