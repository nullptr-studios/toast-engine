using System.Collections.Generic;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace editor.Git;

public partial class CommitView : UserControl {
	public CommitView() {
		InitializeComponent();
	}

	private CommitViewModel? Vm => DataContext as CommitViewModel;

	private List<GitFileRow> SelectedUnstaged => UnstagedList.SelectedItems?.OfType<GitFileRow>().ToList() ?? [];
	private List<GitFileRow> SelectedStaged => StagedList.SelectedItems?.OfType<GitFileRow>().ToList() ?? [];

	private void OnUnstagedDoubleTapped(object? sender, TappedEventArgs e) {
		StageSelected();
	}

	private void OnStagedDoubleTapped(object? sender, TappedEventArgs e) {
		UnstageSelected();
	}

	private void OnUnstagedKeyDown(object? sender, KeyEventArgs e) {
		switch (e.Key) {
			case Key.Enter:
				StageSelected();
				e.Handled = true;
				break;
			case Key.Delete:
				DiscardSelected();
				e.Handled = true;
				break;
		}
	}

	private void OnStagedKeyDown(object? sender, KeyEventArgs e) {
		if (e.Key != Key.Enter) return;
		UnstageSelected();
		e.Handled = true;
	}

	private void OnStageClick(object? sender, RoutedEventArgs e) {
		StageSelected();
	}

	private void OnDiscardClick(object? sender, RoutedEventArgs e) {
		DiscardSelected();
	}

	private void OnUnstageClick(object? sender, RoutedEventArgs e) {
		UnstageSelected();
	}

	private void StageSelected() {
		var rows = SelectedUnstaged;
		if (rows.Count > 0) _ = Vm?.StageAsync(rows);
	}

	private void UnstageSelected() {
		var rows = SelectedStaged;
		if (rows.Count > 0) _ = Vm?.UnstageAsync(rows);
	}

	private void DiscardSelected() {
		var rows = SelectedUnstaged;
		if (rows.Count > 0) _ = Vm?.DiscardAsync(rows);
	}
}
