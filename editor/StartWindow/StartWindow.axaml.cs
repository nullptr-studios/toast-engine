using System;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace editor.StartWindow;

public partial class StartWindow : Window {
	public StartWindow() {
		InitializeComponent();
		Closed += OnWindowClosed;
		Loaded += OnWindowLoaded;

		AddHandler(KeyDownEvent, OnWindowKeyDown, RoutingStrategies.Tunnel);
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

	private void OnSearchKeyDown(object? sender, KeyEventArgs e) {
		if (e.Key != Key.Escape) return;
		if (DataContext is not StartWindowViewModel vm) return;

		e.Handled = true;

		if (string.IsNullOrEmpty(vm.SearchText)) {
			FocusManager?.Focus(null);
			return;
		}

		vm.ResetSearch();
	}

	private void OnProjectListKeyDown(object? sender, KeyEventArgs e) {
		if (e.Key != Key.Delete) return;
		if (sender is not ListBox { SelectedItem: ProjectListItem item }) return;
		if (DataContext is not StartWindowViewModel vm) return;

		e.Handled = true;
		vm.RemoveProjectCommand.Execute(item);
	}

	private void OnWindowKeyDown(object? sender, KeyEventArgs e) {
		if (e.Key != Key.Enter) return;
		if (DataContext is not StartWindowViewModel vm) return;
		if (this.FindControl<ListBox>("ProjectListBox")?.SelectedItem is not ProjectListItem item) return;

		e.Handled = true;
		vm.OpenProjectFromListCommand.Execute(item);
	}
}
