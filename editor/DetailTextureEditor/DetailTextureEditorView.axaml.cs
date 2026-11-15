using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using editor.Workspace;

namespace editor.DetailTextureEditor;

public partial class DetailTextureEditorView : UserControl {
	public DetailTextureEditorView() {
		InitializeComponent();
		AddHandler(PointerPressedEvent, (_, _) => (DataContext as DetailTextureEditorViewModel)?.EnsureEngine(),
			RoutingStrategies.Tunnel, true);
		AddHandler(KeyDownEvent, OnShortcut, RoutingStrategies.Tunnel, true);
	}

	private void OnRowPressed(object? sender, PointerPressedEventArgs e) {
		if (sender is Control { DataContext: DetailEntryVM item } && DataContext is DetailTextureEditorViewModel vm)
			vm.SelectedItem = item;
	}

	private void OnShortcut(object? sender, KeyEventArgs e) {
		if (DataContext is not DetailTextureEditorViewModel vm || e.Source is TextBox || Viewport.IsEditorFlying) return;
		if (e.Key == Key.S && e.KeyModifiers == KeyModifiers.Control) {
			vm.SaveCommand.Execute(null);
			e.Handled = true;
		}
	}
}
