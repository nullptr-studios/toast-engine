using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Avalonia.VisualTree;
using editor.Assets;
using Lucide.Avalonia;

namespace editor.Components.Modals;

public partial class PickerWindow : Window {
	private readonly Dictionary<string, Bitmap?> m_thumbnails = [];
	private object? m_selected;
	private string? m_hoveredThumbnail;

	protected PickerWindow(PickerViewModel vm) {
		DataContext = vm;
		InitializeComponent();
		ItemsList.DoubleTapped += OnItemsDoubleTapped;
		ItemsList.AddHandler(PointerPressedEvent, OnListPointerPressed, RoutingStrategies.Tunnel);

		Title = vm.WindowTitle;
		AcceptLabel.Text = vm.AcceptLabel;
		AcceptIcon.Kind = vm.AcceptIconKind;

		if (vm.ExtraButtonLabel is { } extraLabel) {
			ExtraButton.IsVisible = true;
			ExtraLabel.Text = extraLabel;
			ExtraIcon.Kind = vm.ExtraIconKind;
		}

		ViewToggleButton.IsVisible = vm.HasViewToggle;
		UpdateViewToggle();
		RebuildRows();
	}

	private PickerViewModel ViewModel => (PickerViewModel)DataContext!;

	protected override void OnOpened(EventArgs e) {
		base.OnOpened(e);
		SearchBox.Focus();
	}

	protected override void OnClosed(EventArgs e) {
		base.OnClosed(e);
		ThumbnailPopup.IsOpen = false;
		foreach (var bitmap in m_thumbnails.Values) bitmap?.Dispose();
		m_thumbnails.Clear();
	}

	private void RebuildRows() {
		var keep = m_selected;
		var rows = PickerRow.Flatten(ViewModel.Items);
		HideThumbnail();
		ItemsList.ItemsSource = rows;

		var row = keep is null ? null : rows.FirstOrDefault(r => ReferenceEquals(r.Node, keep));
		ItemsList.SelectedItem = row;
		if (row is not null) ItemsList.ScrollIntoView(row);
		else m_selected = null;
	}

	private void OnSearchChanged(object? sender, TextChangedEventArgs e) {
		var query = SearchBox.Text ?? "";
		ViewModel.UpdateFilter(query, query.Any(char.IsUpper));
		RebuildRows();
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
			() => {
				if (ItemsList.SelectedItem is null && ItemsList.ItemCount > 0) ItemsList.SelectedIndex = 0;
				ItemsList.Focus();
			},
			DispatcherPriority.Background);
	}

	private void OnViewToggle(object? sender, RoutedEventArgs e) {
		ViewModel.IsTreeMode = !ViewModel.IsTreeMode;
		UpdateViewToggle();
		RebuildRows();
	}

	private void UpdateViewToggle() {
		var tree = ViewModel.IsTreeMode;
		ViewToggleIcon.Kind = tree ? LucideIconKind.List : LucideIconKind.ListTree;
		ToolTip.SetTip(ViewToggleButton, tree ? "Show as a list" : "Show as folders");
	}

	private void OnSelectionChanged(object? sender, SelectionChangedEventArgs e) {
		var item = (ItemsList.SelectedItem as PickerRow)?.Node;
		if (!ViewModel.IsSelectable(item)) {
			// folders and other non pickable rows can't stay selected
			if (ItemsList.SelectedItem is not null) ItemsList.SelectedItem = null;
			m_selected = null;
		} else {
			m_selected = item;
		}
	}

	private void OnListPointerPressed(object? sender, PointerPressedEventArgs e) {
		if ((e.Source as Visual)?.FindAncestorOfType<Panel>(true) is not { } panel || !panel.Classes.Contains("chevron"))
			return;
		if (panel.DataContext is not PickerRow row) return;

		ToggleRow(row);
		e.Handled = true;
	}

	private void ToggleRow(PickerRow row) {
		if (!row.HasChildren || row.Node is not IPickerNode node) return;
		node.IsExpanded = !node.IsExpanded;
		RebuildRows();
	}

	private void OnListKeyDown(object? sender, KeyEventArgs e) {
		if (e.KeyModifiers != KeyModifiers.None || ItemsList.SelectedItem is not PickerRow row) return;
		if (row.Node is not IPickerNode node || !row.HasChildren) return;

		switch (e.Key) {
			case Key.Right when !node.IsExpanded:
			case Key.Left when node.IsExpanded:
				ToggleRow(row);
				e.Handled = true;
				break;
		}
	}

	private void OnItemsDoubleTapped(object? sender, TappedEventArgs e) {
		if ((e.Source as Visual)?.FindAncestorOfType<ListBoxItem>(true)?.DataContext is not PickerRow row) return;

		if (!ViewModel.IsSelectable(row.Node)) {
			ToggleRow(row);
			return;
		}

		m_selected = row.Node;
		Dispatcher.UIThread.Post(() => TryAccept(row.Node), DispatcherPriority.Background);
	}

	private void OnAccept(object? sender, RoutedEventArgs e) {
		TryAccept(m_selected);
	}

	private async void OnExtraButton(object? sender, RoutedEventArgs e) {
		await ViewModel.OnExtraButton(this, m_selected);
		RebuildRows();
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(null);
	}

	private void TryAccept(object? selected) {
		if (ViewModel.GetResult(selected) is { } result) Close(result);
	}

	private void OnListPointerMoved(object? sender, PointerEventArgs e) {
		var item = (e.Source as Visual)?.FindAncestorOfType<ListBoxItem>(true);
		if (item?.DataContext is not PickerRow { ThumbnailUid: { } uid }) {
			HideThumbnail();
			return;
		}

		if (uid == m_hoveredThumbnail) return;
		m_hoveredThumbnail = uid;
		ThumbnailPopup.IsOpen = false;
		_ = ShowThumbnailAsync(uid, item);
	}

	private void OnListPointerExited(object? sender, PointerEventArgs e) {
		HideThumbnail();
	}

	private void HideThumbnail() {
		m_hoveredThumbnail = null;
		ThumbnailPopup.IsOpen = false;
	}

	private async Task ShowThumbnailAsync(string uid, Control target) {
		if (!m_thumbnails.TryGetValue(uid, out var bitmap)) {
			var path = ProjectContext.IsInitialized
				? Path.Combine(ProjectContext.CachePath, "thumbnails", uid + ".png")
				: null;
			bitmap = path is null ? null : await Task.Run(() => LoadThumbnail(path));
			m_thumbnails[uid] = bitmap;
		}

		// the pointer moved on while it loaded
		if (bitmap is null || m_hoveredThumbnail != uid || !target.IsEffectivelyVisible) return;
		ThumbnailImage.Source = bitmap;
		ThumbnailPopup.PlacementTarget = target;
		ThumbnailPopup.IsOpen = true;
	}

	private static Bitmap? LoadThumbnail(string path) {
		try {
			return File.Exists(path) ? new Bitmap(path) : null;
		} catch {
			return null;
		}
	}
}
