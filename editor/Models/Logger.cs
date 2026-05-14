using System;
using System.Collections.Generic;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using editor.ViewModels;

namespace editor.Models;

public class LogClient {
	public event Action<List<LogEntry>> on_log_received;
	private TcpClient m_client;
	private CancellationTokenSource m_cts;

	public void Start() {
		m_cts = new CancellationTokenSource();
		Task.Run(() => ListenLoop(m_cts.Token));
	}

	public void Stop() {
		m_cts?.Cancel();
		m_client?.Close();
	}

	private async Task ListenLoop(CancellationToken token) {
		try {
			m_client = new TcpClient();
			await m_client.ConnectAsync("127.0.0.1", 12801, token);
			using var stream = m_client.GetStream();

			while (!token.IsCancellationRequested) {
				// TODO: Handle parsing
			}
		}
		catch (Exception ex) {
			// TODO: Handle disconects
		}
	}
}