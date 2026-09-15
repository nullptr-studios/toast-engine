using Avalonia.Controls;
using Avalonia.Input;

namespace editor.Workspace;

public partial class HistoryView : UserControl {
	public HistoryView() {
		InitializeComponent();
	}

	private void OnRowPointerPressed(object? sender, PointerPressedEventArgs e) {
		if (sender is not Control { DataContext: HistoryRowViewModel row } control ||
		    DataContext is not HistoryViewModel vm) return;
		var properties = e.GetCurrentPoint(control).Properties;
		if (properties.IsRightButtonPressed) {
			vm.SelectedRevision = row;
			return;
		}

		if (properties.IsLeftButtonPressed && e.ClickCount == 2) {
			vm.Activate(row);
			e.Handled = true;
		}
	}
}
