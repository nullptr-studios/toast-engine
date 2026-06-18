using Avalonia.Controls;
using Avalonia.Interactivity;

namespace editor.Workspace;

public enum SaveChangesResult { Cancel, Save, DontSave }

public partial class SaveChangesDialog : Window {
	public SaveChangesDialog() {
		InitializeComponent();
	}

	public SaveChangesDialog(string name) {
		InitializeComponent();
		MessageText.Text = $"Save changes to \"{name}\" before closing?";
	}

	private void OnSave(object? sender, RoutedEventArgs e) {
		Close(SaveChangesResult.Save);
	}

	private void OnDontSave(object? sender, RoutedEventArgs e) {
		Close(SaveChangesResult.DontSave);
	}

	private void OnCancel(object? sender, RoutedEventArgs e) {
		Close(SaveChangesResult.Cancel);
	}
}
