//
// LogsViewModel.cs by Xein
// 14 May 2026
//

using System.Collections.Generic;
using System.Collections.ObjectModel;
using Avalonia.Threading;
using Dock.Model.Mvvm.Controls;

namespace editor.Logger;

public class LogEntry {
	public string? Sink { get; set; }
	public string? Message { get; set; }
	public string? Timestamp { get; set; }
	public string? File { get; set; }
	public uint? Severity { get; set; }
}

public class LogsViewModel : Tool {
	private readonly LogClient m_client;

	public LogsViewModel() {
		LogEntries = [];
		m_client = new LogClient();
		m_client.OnLogReceived += HandleNewLogs;
	}

	public ObservableCollection<LogEntry> LogEntries { get; set; }

	public void Start() {
		m_client.start();
	}

	public void Stop() {
		m_client.stop();
	}

	private void HandleNewLogs(List<LogEntry> newLogs) {
		Dispatcher.UIThread.InvokeAsync(() => {
			foreach (var l in newLogs) LogEntries.Add(l);

			// TODO: Lock scroll, filters, etc should be here
		});
	}
}
