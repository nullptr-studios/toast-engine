using System;
using Avalonia.Controls;
using Avalonia.Interactivity;
using editor.ViewModels;

namespace editor.Views;

public partial class StartWindow : Window {
	public StartWindow() {
		InitializeComponent();
		this.Closed += OnWindowClosed;
	}

	private void OnWindowClosed(object? sender, EventArgs e) {
		if (DataContext is StartWindowViewModel vm) {
			vm.saveProjects();
		}
	}
}
