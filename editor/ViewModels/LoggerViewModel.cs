using System.Collections.Generic;
using System.Collections.ObjectModel;
using Avalonia.Threading;
using editor.Models;

namespace editor.ViewModels;

public class LogEntry {
	public string sink {get; set;}
	public string message {get; set;}
	public string timestamp {get; set;}
	public string file {get; set;}
	public uint severity {get; set;}
}

public partial class LoggerViewModel : ViewModelBase  {
	public ObservableCollection<LogEntry> log_entries {get; set;}
	private LogClient m_client;

	public LoggerViewModel() {
		log_entries = [];
		m_client = new LogClient();
		m_client.on_log_received += handleNewLogs;
	}

	public void start() => m_client.Start();
	public void stop() => m_client.Stop();

	private void handleNewLogs(List<LogEntry> new_logs) {
		Dispatcher.UIThread.InvokeAsync(() => {
			foreach (var l in new_logs) {
				log_entries.Add(l);
			}

			// TODO: Lock scroll, filters, etc should be here
		});
	}
}
