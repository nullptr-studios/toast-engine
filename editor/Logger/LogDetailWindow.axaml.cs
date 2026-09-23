//
// LogDetailWindow.axaml.cs by Xein
// 28 Jul 2026
//

using Avalonia.Controls;
using Avalonia.Input.Platform;
using Avalonia.Interactivity;

namespace editor.Logger;

public partial class LogDetailWindow : Window {
	public LogDetailWindow() {
		InitializeComponent();
	}

	public LogDetailWindow(LogEntry entry) : this() {
		DataContext = entry;
	}

	private LogEntry? Entry => DataContext as LogEntry;

	private async void OnCopy(object? sender, RoutedEventArgs e) {
		if (Entry is { } entry && Clipboard is { } clipboard)
			await clipboard.SetTextAsync(LogCsv.ToCsvLine(entry));
	}

	private async void OnCopyMessage(object? sender, RoutedEventArgs e) {
		if (Entry is { } entry && Clipboard is { } clipboard)
			await clipboard.SetTextAsync(entry.Message);
	}

	private void OnClose(object? sender, RoutedEventArgs e) {
		Close();
	}
}
