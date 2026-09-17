using System.Diagnostics;
using Avalonia.Controls;
using Avalonia.Input.Platform;
using Avalonia.Interactivity;

namespace crash_reporter;

public partial class CrashWindow : Window {
	private readonly CrashTarget? m_target;
	private CrashReport? m_report;
	private string? m_lastDump;

	public CrashWindow() {
		InitializeComponent();
	}

	internal CrashWindow(CrashTarget target) : this() {
		m_target = target;
		TitleText.Text = $"{target.ProcessName} crashed";
		StatusText.Text = "Resolving symbols...";
		_ = LoadReportAsync();
	}

	private async Task LoadReportAsync() {
		if (m_target is null) return;

		// dbghelp loads every PDB which takes seconds for the engine
		m_report = await Task.Run(() => CrashReport.Build(m_target));

		ReasonText.Text = m_report.Reason;
		MessageText.Text = m_report.Message;
		MessageText.IsVisible = !string.IsNullOrEmpty(m_report.Message);
		StackText.Text = m_report.StackText;
		StatusText.Text = m_report.Error ?? $"{m_report.Frames.Count} frames";
		CopyButton.IsEnabled = true;
		DumpButton.IsEnabled = m_target.IsOpen;

		// Test hook that dumps without the window
		if (Environment.GetEnvironmentVariable("TOAST_CRASH_REPORTER_AUTODUMP") == "1") {
			await SaveDumpAsync();
		}
	}

	private async Task SaveDumpAsync() {
		if (m_target is null || m_report is null) return;

		DumpButton.IsEnabled = false;
		StatusText.Text = "Writing full dump, this can take a while...";
		try {
			var report = m_report;
			var target = m_target;
			m_lastDump = await Task.Run(() => {
				var path = target.WriteFullDump();
				File.WriteAllText(Path.ChangeExtension(path, ".txt"), report.Text);
				return path;
			});

			var size = new FileInfo(m_lastDump).Length / (1024.0 * 1024.0);
			StatusText.Text = $"Saved {m_lastDump} ({size:F0} MiB)";
			OpenFolderButton.IsVisible = true;
		} catch (Exception e) {
			StatusText.Text = $"Dump failed: {e.Message}";
			DumpButton.IsEnabled = true;
		}
	}

	private async void OnCopy(object? sender, RoutedEventArgs e) {
		if (m_report is null || Clipboard is null) return;
		await Clipboard.SetTextAsync(m_report.Text);
		StatusText.Text = "Copied to the clipboard";
	}

	private async void OnSaveDump(object? sender, RoutedEventArgs e) {
		await SaveDumpAsync();
	}

	private void OnOpenFolder(object? sender, RoutedEventArgs e) {
		if (m_lastDump is null) return;
		Process.Start(new ProcessStartInfo("explorer.exe", $"/select,\"{m_lastDump}\"") { UseShellExecute = false });
	}

	private void OnClose(object? sender, RoutedEventArgs e) {
		Close();
	}
}
