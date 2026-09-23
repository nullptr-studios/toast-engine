using System;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Threading;

namespace editor.Components.Modals;

public partial class PickerWindow : Window {
	private object? m_selected;

	protected PickerWindow(PickerViewModel vm) {
		DataContext = vm;
		InitializeComponent();
		ItemsTree.DoubleTapped += OnItemsDoubleTapped;

		Title = vm.WindowTitle;
		AcceptLabel.Text = vm.AcceptLabel;
		AcceptIcon.Kind = vm.AcceptIconKind;

		if (vm.ExtraButtonLabel is { } extraLabel) {
			ExtraButton.IsVisible = true;
			ExtraLabel.Text = extraLabel;
			ExtraIcon.Kind = vm.ExtraIconKind;
		}
	}

	private PickerViewModel ViewModel => (PickerViewModel)DataContext!;

	protected override void OnOpened(EventArgs e) {
		base.OnOpened(e);
		SearchBox.Focus();
	}

	private void OnSearchChanged(object? sender, TextChangedEventArgs e) {
		var query = SearchBox.Text ?? "";
		ViewModel.UpdateFilter(query, query.Any(char.IsUpper));
	}

	private void OnSearchKeyDown(object? sender, KeyEventArgs e) {
		if (e.Key == Key.Escape) {
			if (!string.IsNullOrEmpty(SearchBox.Text))
				SearchBox.Text = "";
			else
				FocusResults();
			e.Handled = true;
		} else if (e.Key == Key.Enter) {
			FocusResults();
			e.Handled = true;
		}
	}

	private void FocusResults() {
		Dispatcher.UIThread.Post(
			() => ItemsTree.Focus(),
			DispatcherPriority.Background);
	}

	private void OnSelectionChanged(object? sender, SelectionChangedEventArgs e) {
		var item = ItemsTree.SelectedItem;
		if (!ViewModel.IsSelectable(item)) {
			ItemsTree.SelectedItem = null;
			m_selected = null;
		} else {
			m_selected = item;
		}
	}

	private void OnItemsDoubleTapped(object? sender, TappedEventArgs e) {
		var item = ItemsTree.SelectedItem;
		if (!ViewModel.IsSelectable(item)) return;
		m_selected = item;
		Dispatcher.UIThread.Post(() => TryAccept(item), DispatcherPriority.Background);
	}

	private void OnAccept(object? sender, RoutedEventArgs e) {
		TryAccept(m_selected);
	}

	private async void OnExtraButton(object? sender, RoutedEventArgs e) {
		await ViewModel.OnExtraButton(this, m_selected);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}

	private void TryAccept(object? selected) {
		if (ViewModel.GetResult(selected) is { } result) Close(result);
	}
}
