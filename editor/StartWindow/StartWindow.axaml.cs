using System;
using Avalonia.Controls;
using Avalonia.Interactivity;

namespace editor.StartWindow;

public partial class StartWindow : Window {
	public StartWindow() {
		InitializeComponent();
		Closed += OnWindowClosed;
		Loaded += OnWindowLoaded;
	}

	private void OnWindowLoaded(object? sender, RoutedEventArgs e) {
		if (DataContext is StartWindowViewModel vm) vm.SetWindow(this);
		if (this.FindControl<ListBox>("ProjectListBox") is { } listBox) listBox.DoubleTapped += OnProjectDoubleClicked;
	}

	private void OnWindowClosed(object? sender, EventArgs e) {
		if (DataContext is StartWindowViewModel vm) vm.SaveProjects();
	}

	private void OnProjectDoubleClicked(object? sender, RoutedEventArgs e) {
		if (sender is ListBox listBox && listBox.SelectedItem is ProjectListItem item)
			if (DataContext is StartWindowViewModel vm)
				vm.OpenProjectFromListCommand.Execute(item);
	}
}
