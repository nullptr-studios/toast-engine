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
	public string sink { get; set; }
	public string message { get; set; }
	public string timestamp { get; set; }
	public string file { get; set; }
	public uint severity { get; set; }
}

public class LogsViewModel : Tool {
	private readonly LogClient m_client;

	public LogsViewModel() {
		log_entries = [];
		m_client = new LogClient();
		m_client.OnLogReceived += handleNewLogs;
	}

	public ObservableCollection<LogEntry> log_entries { get; set; }

	public void start() {
		m_client.start();
	}

	public void stop() {
		m_client.stop();
	}

	private void handleNewLogs(List<LogEntry> new_logs) {
		Dispatcher.UIThread.InvokeAsync(() => {
			foreach (var l in new_logs) log_entries.Add(l);

			// TODO: Lock scroll, filters, etc should be here
		});
	}
}
