using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;

namespace editor.Components.Modals;

public enum UnsavedChangesResult { Cancel, DiscardAll, SaveAll }

public record UnsavedItem(string Kind, string Name, Func<Task<bool>> Save, Action Discard);

public record UnsavedChangesRow(string Name, string Kind);

public class UnsavedChangesViewModel {
	// Fake data for the previewer
	public UnsavedChangesViewModel() : this([new UnsavedItem("Node", "Level_01.tnode", () => Task.FromResult(true), () => { })]) { }

	public UnsavedChangesViewModel(IReadOnlyList<UnsavedItem> items) {
		Rows = items.Select(i => new UnsavedChangesRow(i.Name, i.Kind)).ToList();
		Message = items.Count == 1
			? "There is 1 file with unsaved changes. Save it before closing?"
			: $"There are {items.Count} files with unsaved changes. Save them before closing?";
	}

	public string Title => "Unsaved changes";
	public string Message { get; }
	public IReadOnlyList<UnsavedChangesRow> Rows { get; }
}

public partial class UnsavedChangesModal : Window {
	public UnsavedChangesModal() {
		InitializeComponent();
		DataContext = new UnsavedChangesViewModel();
	}

	public UnsavedChangesModal(IReadOnlyList<UnsavedItem> items) {
		InitializeComponent();
		DataContext = new UnsavedChangesViewModel(items);
	}

	protected override void OnOpened(EventArgs e) {
		base.OnOpened(e);
		SaveAllButton.Focus();
	}

	private void OnSaveAll(object? sender, RoutedEventArgs e) {
		Close(UnsavedChangesResult.SaveAll);
	}

	private void OnDiscardAll(object? sender, RoutedEventArgs e) {
		Close(UnsavedChangesResult.DiscardAll);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(UnsavedChangesResult.Cancel);
	}
}
