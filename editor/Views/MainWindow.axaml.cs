using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using editor.Models;
using editor.ViewModels;

namespace editor.Views;

public partial class MainWindow : Window {
	private Window? m_logs_window;
	private ToastEngine? m_toast;

	public MainWindow() {
		InitializeComponent();
	}

	public MainWindow(ToastEngine toast) {
		InitializeComponent();
		m_toast = toast;
	}

	protected override void OnClosed(EventArgs e) {
		base.OnClosed(e);
		m_toast?.removeWindow(this);
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change) {
		base.OnPropertyChanged(change);
		if (change.Property == WindowDecorationMarginProperty && menuBorder is not null) {
			var margin = (Thickness)change.NewValue!;
			menuBorder.Margin = new Thickness(margin.Left, 0, 0, 0);
		}
	}

	private void onTitleBarPointerPressed(object? sender, PointerPressedEventArgs e) {
		if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
			BeginMoveDrag(e);
	}

	private void onMinimize(object? sender, RoutedEventArgs e) =>
		WindowState = WindowState.Minimized;

	private void onMaximize(object? sender, RoutedEventArgs e) =>
		WindowState = WindowState == WindowState.Maximized
			? WindowState.Normal
			: WindowState.Maximized;

	private void onClose(object? sender, RoutedEventArgs e) => Close();

	private void onLogWindowButton(object? sender, RoutedEventArgs e) {
		if (log_window_button.IsChecked) {
			if (m_logs_window is null) {
				m_logs_window = new LogsWindow {
					DataContext = new LoggerViewModel()
				};
				m_logs_window.Closed += (s, args) => {
					log_window_button.IsChecked = false;
					m_logs_window = null;
				};
			}
			m_logs_window.Show();
		} else {
			m_logs_window?.Close();
		}
	}
}
