using System.Linq;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Lucide.Avalonia;

namespace editor.Components.Modals;

public partial class PickerWindow : Window {
	private object? m_selected;

	protected PickerWindow(PickerViewModel vm) {
		DataContext = vm;
		InitializeComponent();

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

	private void OnSearchChanged(object? sender, TextChangedEventArgs e) {
		var query = SearchBox.Text ?? "";
		ViewModel.UpdateFilter(query, query.Any(char.IsUpper));
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

	private void OnItemDoubleTapped(object? sender, TappedEventArgs e) {
		TryAccept();
	}

	private void OnAccept(object? sender, RoutedEventArgs e) {
		TryAccept();
	}

	private async void OnExtraButton(object? sender, RoutedEventArgs e) {
		await ViewModel.OnExtraButton(this, m_selected);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}

	private void TryAccept() {
		if (ViewModel.GetResult(m_selected) is { } result) Close(result);
	}
}
