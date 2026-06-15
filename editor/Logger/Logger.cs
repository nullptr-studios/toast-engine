//
// Logger.cs by Xein
// 14 May 2026
//

using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using Proto.Logging;

namespace editor.Logger;

public class LogClient {
	private TcpClient m_client;
	private CancellationTokenSource m_cts;
	public event Action<List<LogEntry>> OnLogReceived;

	public void start() {
		m_cts = new CancellationTokenSource();
		Console.WriteLine("Starting TCP loop");
		Task.Run(() => listenLoop(m_cts.Token));
	}

	public void stop() {
		m_cts?.Cancel();
		m_client?.Close();
	}

	private async Task listenLoop(CancellationToken token) {
		try {
			m_client = new TcpClient();
			await m_client.ConnectAsync("127.0.0.1", 12801, token);
			Console.WriteLine("Connected");
			using var stream = m_client.GetStream();

			var length_buffer = new byte[4];

			while (!token.IsCancellationRequested) {
				// Read length prefix
				await readExactlyAsync(stream, length_buffer, 4, token);
				var message_length = BinaryPrimitives.ReadUInt32BigEndian(length_buffer);
				if (message_length == 0) continue;

				// Parse protocol buffer
				var message_buffer = new byte[message_length];
				await readExactlyAsync(stream, message_buffer, (int)message_length, token);
				var proto_batch = LogBatch.Parser.ParseFrom(message_buffer);
				// Console.WriteLine($"Received {proto_batch.Logs.Count} logs");

				// Map to avalonia type
				List<LogEntry> batch = [];
				foreach (var log_data in proto_batch.Logs) {
					var entry = new LogEntry {
						message = log_data.Message,
						severity = (uint)log_data.Severity,
						file = log_data.Filepath + ":" + log_data.LineNumber,
						sink = log_data.Sink,
						timestamp = DateTimeOffset.FromUnixTimeMilliseconds((long)(log_data.Timestamp / 1_000_000))
							.ToLocalTime().ToString("HH:mm:ss.fff")
					};
					batch.Add(entry);
				}

				OnLogReceived?.Invoke(batch);
			}
		} catch (EndOfStreamException) {
			Console.WriteLine("Rust server closed connection");
		} catch (Exception ex) when (ex is not OperationCanceledException) {
			Console.WriteLine($"Error in TCP loop: {ex.Message}");
		} finally {
			m_client.Close();
		}
	}

	private static async Task readExactlyAsync(
		NetworkStream stream, byte[] buffer, int bytesToRead, CancellationToken token) {
		var total_bytes_read = 0;
		while (total_bytes_read < bytesToRead) {
			var bytes_read =
				await stream.ReadAsync(buffer.AsMemory(total_bytes_read, bytesToRead - total_bytes_read), token);
			if (bytes_read == 0) throw new EndOfStreamException("TCP connection closed unexpectedly while reading");
			total_bytes_read += bytes_read;
		}
	}
}
