//
// LogEntry.cs by Xein
// 28 Jul 2026
//

using System;
using System.Globalization;
using System.IO;

namespace editor.Logger;

public sealed class LogEntry {
	private const char FieldSeparator = '';
	private const int MaxDisplayMessageLength = 500;

	public LogEntry(ulong timestampNanos, uint severity, string filepath, uint line, string sink, string message) {
		TimestampNanos = timestampNanos;
		Severity = severity;
		Filepath = filepath;
		Line = line;
		Sink = sink;
		Message = message;

		SeverityName = LogCsv.SeverityName(severity);
		SeverityShortName = severity switch {
			2 => "WARN",
			4 => "CRIT",
			_ => SeverityName
		};

		var local = DateTimeOffset.UnixEpoch.AddTicks((long)(timestampNanos / 100)).ToLocalTime();
		Time = local.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture);
		PreciseTime = local.ToString("HH:mm:ss.ffffff", CultureInfo.InvariantCulture); // microsecond precision
		AbsoluteTimestamp = local.ToString("yyyy-MM-dd HH:mm:ss.fffffff", CultureInfo.InvariantCulture);

		FileLine = line > 0 ? $"{filepath}:{line}" : filepath;
		var filename = Path.GetFileName(filepath.Replace('\\', '/'));
		DisplayFileLine = line > 0 ? $"{filename}:{line}" : filename;

		DisplayMessage = TrimForTable(message);
		SearchBlob = string.Join(FieldSeparator, SeverityName, sink, FileLine, message);
	}

	public ulong TimestampNanos { get; }
	public uint Severity { get; }
	public string Filepath { get; }
	public uint Line { get; }
	public string Sink { get; }
	public string Message { get; }

	public string SeverityName { get; }
	public string SeverityShortName { get; }
	public string Time { get; }
	public string PreciseTime { get; }
	public string AbsoluteTimestamp { get; }
	public string FileLine { get; }
	public string DisplayFileLine { get; }
	public string DisplayMessage { get; }
	public string SearchBlob { get; }

	private static string TrimForTable(string message) {
		var flattened = message.IndexOfAny(['\r', '\n']) < 0
			? message
			: message.Replace("\r\n", " ⏎ ").Replace('\r', '⏎').Replace('\n', '⏎');

		return flattened.Length > MaxDisplayMessageLength
			? string.Concat(flattened.AsSpan(0, MaxDisplayMessageLength), "…")
			: flattened;
	}
}
