using Avalonia.Controls;
using Avalonia.Interactivity;
using editor.ViewModels;

namespace editor.Views;

public partial class MainWindow : Window {
	private Window? m_logs_window;
	public MainWindow() {
		InitializeComponent();
	}

	private void onLogWindowButton(object? sender, RoutedEventArgs e) {
		if (log_window_button.IsChecked) {
			// If there's no window create it, otherwise, do nothing
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