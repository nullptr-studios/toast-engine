using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using editor.Assets.Importers;
using editor.Components.Elements;

namespace editor.Assets;

public partial class AssetBrowserView : UserControl {
	private const double DragThreshold = 4;

	private AssetFile? m_pressFile;
	private Point m_pressPoint;
	private PointerPressedEventArgs? m_pressArgs;

	public AssetBrowserView() {
		InitializeComponent();
	}

	private AssetBrowserViewModel Vm => (AssetBrowserViewModel)DataContext!;

	private void OnFilePointerPressed(object? sender, PointerPressedEventArgs e) {
		if (sender is not Control { DataContext: AssetFile file }) return;
		if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		m_pressFile = file;
		m_pressArgs = e;
		m_pressPoint = e.GetPosition(this);
	}

	private async void OnFilePointerMoved(object? sender, PointerEventArgs e) {
		if (m_pressFile is null || m_pressArgs is null || !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		var delta = e.GetPosition(this) - m_pressPoint;
		if (delta.X * delta.X + delta.Y * delta.Y < DragThreshold * DragThreshold) return;

		var file = m_pressFile;
		var args = m_pressArgs;
		m_pressFile = null;
		m_pressArgs = null;

		if (file.Uid is not { } uid) return; // no .meta -> nothing stable to reference

		var data = new DataTransfer();
		data.Add(DataTransferItem.Create(AssetDragData.Format, new AssetDragRef(uid, file.Type, file.Name)));
		await DragDrop.DoDragDropAsync(args, data, DragDropEffects.Copy);
	}

	private void OnFilePointerReleased(object? sender, PointerReleasedEventArgs e) {
		m_pressFile = null;
		m_pressArgs = null;
	}

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
