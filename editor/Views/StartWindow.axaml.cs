using System;
using Avalonia.Controls;
using Avalonia.Interactivity;
using editor.ViewModels;
using editor.Models;

namespace editor.Views;

public partial class StartWindow : Window {
	public StartWindow() {
		InitializeComponent();
		this.Closed += OnWindowClosed;
		this.Loaded += OnWindowLoaded;
	}

	private void OnWindowLoaded(object? sender, RoutedEventArgs e) {
		if (DataContext is StartWindowViewModel vm) {
			vm.setWindow(this);
		}
		if (this.FindControl<ListBox>("ProjectListBox") is ListBox listBox) {
			listBox.DoubleTapped += OnProjectDoubleClicked;
		}
	}

	private void OnWindowClosed(object? sender, EventArgs e) {
		if (DataContext is StartWindowViewModel vm) {
			vm.saveProjects();
		}
	}

	private void OnProjectDoubleClicked(object? sender, RoutedEventArgs e) {
		if (sender is ListBox listBox && listBox.SelectedItem is ProjectListItem item) {
			if (DataContext is StartWindowViewModel vm) {
				vm.openProjectFromListCommand.Execute(item);
			}
		}
	}
}
