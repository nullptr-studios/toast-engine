using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using Lucide.Avalonia;

namespace editor.Services;

public class ModalPopupViewModel : ObservableObject {
	public string Title { get; init; } = "";
	public string Message { get; init; } = "";
	public bool ShowCancel { get; init; }
	public LucideIconKind IconKind { get; init; }
	public IBrush IconColor { get; init; } = Brushes.White;
}

public partial class ModalPopupWindow : Window {
	public ModalPopupWindow() {
		InitializeComponent();
	}

	public ModalPopupWindow(ModalPopupViewModel vm) {
		InitializeComponent();
		DataContext = vm;
	}

	private void OnOk(object? sender, RoutedEventArgs e) {
		Close(true);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(false);
	}
}
