using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using editor.ViewModels;
using proto.logging;

namespace editor.Models;

public class LogClient {
	public event Action<List<LogEntry>> on_log_received;
	private TcpClient m_client;
	private CancellationTokenSource m_cts;

	public void Start() {
		m_cts = new CancellationTokenSource();
		Console.WriteLine("Starting TCP loop");
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
            Console.WriteLine("Connected");
            using var stream = m_client.GetStream();

            byte[] lengthBuffer = new byte[4];

            while (!token.IsCancellationRequested) {
                // Read length prefix
                await ReadExactlyAsync(stream, lengthBuffer, 4, token);
                uint messageLength = BinaryPrimitives.ReadUInt32BigEndian(lengthBuffer);
                if (messageLength == 0) continue;

                // Parse protocol buffer
                byte[] messageBuffer = new byte[messageLength];
                await ReadExactlyAsync(stream, messageBuffer, (int)messageLength, token);
                LogBatch protoBatch = LogBatch.Parser.ParseFrom(messageBuffer);
                Console.WriteLine($"Received {protoBatch.Logs.Count} logs");

                // Map to avalonia type
                List<LogEntry> batch = [];
                foreach (var logData in protoBatch.Logs) {
                    var entry = new LogEntry {
                        message = logData.Message,
                        severity = (uint)logData.Severity,
                        file = logData.Filepath + ":" + logData.LineNumber,
                        sink = logData.Sink,
                        timestamp = DateTimeOffset.FromUnixTimeMilliseconds((long)(logData.Timestamp / 1_000_000)).ToLocalTime().ToString("HH:mm:ss.fff")
                    };
                    batch.Add(entry);
                }
                on_log_received?.Invoke(batch);
            }
        }
        catch (EndOfStreamException) {
            Console.WriteLine("Rust server closed connection");
        }
        catch (Exception ex) when (ex is not OperationCanceledException) {
            Console.WriteLine($"Error in TCP loop: {ex.Message}");
        }
        finally {
            m_client.Close();
        }
    }

    private static async Task ReadExactlyAsync(NetworkStream stream, byte[] buffer, int bytesToRead, CancellationToken token) {
        int totalBytesRead = 0;
        while (totalBytesRead < bytesToRead) {
            int bytesRead = await stream.ReadAsync(buffer.AsMemory(totalBytesRead, bytesToRead - totalBytesRead), token);
            if (bytesRead == 0) {
                throw new EndOfStreamException("TCP connection closed unexpectedly while reading");
            }
            totalBytesRead += bytesRead;
        }
    }

}