using System;
using Avalonia.Controls;
using editor.ViewModels;

namespace editor.Views;

public partial class LogsWindow : Window {
	public LogsWindow() {
		InitializeComponent();
	}

	protected override void OnOpened(EventArgs e) {
		base.OnOpened(e);

		if (DataContext is LoggerViewModel vm) {
			vm.start();
		}
	}

	protected override void OnClosed(EventArgs e) {
		base.OnClosed(e);

		if (DataContext is LoggerViewModel vm) {
			vm.stop();
		}
	}
}
