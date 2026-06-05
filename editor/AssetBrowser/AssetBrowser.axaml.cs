using System.Collections;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace editor.AssetBrowser;

public partial class AssetBrowser : Window {
    public AssetBrowser() {
        InitializeComponent();
    }

    private AssetBrowserViewModel Vm => (AssetBrowserViewModel)DataContext!;

    private void OnSelectionChanged(object? sender, SelectionChangedEventArgs e) {
        if (sender is ListBox lb)
            Vm.UpdateSelection(lb.SelectedItems);
    }

    private void OnFolderDoubleTapped(object? sender, TappedEventArgs e) {
        if (sender is Border { DataContext: AssetFolder folder }) {
            Vm.SearchText    = "";
            Vm.SelectedFolder = folder;
            e.Handled = true;
        }
    }
}
