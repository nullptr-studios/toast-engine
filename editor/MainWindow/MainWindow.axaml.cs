//
// MainWindow.axaml.cs by Xein
// 2 Jun 2026
//

using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using editor.Logger;

namespace editor.MainWindow;

public partial class MainWindow : Window {
	private Window? m_logsWindow;
	private readonly ToastEngine? m_toast;

	public MainWindow() {
		InitializeComponent();
	}

	public MainWindow(ToastEngine toast) {
		InitializeComponent();
		m_toast = toast;
		EditorDockControl.HostWindowFactory = () => new ToastHostWindow(toast);
	}

	protected override void OnClosed(EventArgs e) {
		base.OnClosed(e);

		if (DataContext is MainWindowViewModel vm) {
			vm.CloseLayout();
		}

		m_toast?.RemoveWindow(this);
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == WindowDecorationMarginProperty && MenuBorder is not null) {
			var margin = (Thickness)change.NewValue!;
			MenuBorder.Margin = new Thickness(margin.Left, 0, 0, 0);
		}
	}

	private void OnTitleBarPointerPressed(object? sender, PointerPressedEventArgs e) {
		if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
			BeginMoveDrag(e);
	}

	private void OnMinimize(object? sender, RoutedEventArgs e) =>
		WindowState = WindowState.Minimized;

	private void OnMaximize(object? sender, RoutedEventArgs e) =>
		WindowState = WindowState == WindowState.Maximized
			? WindowState.Normal
			: WindowState.Maximized;

	private void OnClose(object? sender, RoutedEventArgs e) => Close();

	private void OnLogWindowButton(object? sender, RoutedEventArgs e) {
		if (LogWindowButton.IsChecked) {
			if (m_logsWindow is null) {
				m_logsWindow = new LogsWindow {
					DataContext = new LoggerViewModel()
				};
				m_logsWindow.Closed += (_, _) => {
					LogWindowButton.IsChecked = false;
					m_logsWindow = null;
				};
			}
			m_logsWindow.Show();
		} else {
			m_logsWindow?.Close();
		}
	}
}
