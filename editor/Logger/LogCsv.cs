//
// LogCsv.cs by Xein
// 28 Jul 2026
//

using System;
using System.Buffers;
using System.Globalization;
using System.Text;

namespace editor.Logger;

// Mirrors the CSV format written by the log_server
internal static class LogCsv {
	private static readonly string[] s_severityNames = ["TRACE", "INFO", "WARNING", "ERROR", "CRITICAL"];

	private static readonly SearchValues<char> s_mustQuote = SearchValues.Create(",\"\r\n");

	public static string SeverityName(uint severity) {
		return severity < (uint)s_severityNames.Length ? s_severityNames[severity] : "UNKNOWN";
	}

	public static string ToCsvLine(LogEntry entry) {
		var sb = new StringBuilder(160);
		AppendField(sb, entry.TimestampNanos.ToString(CultureInfo.InvariantCulture));
		sb.Append(',');
		AppendField(sb, SeverityName(entry.Severity));
		sb.Append(',');
		AppendField(sb, entry.Filepath);
		sb.Append(',');
		AppendField(sb, entry.Line.ToString(CultureInfo.InvariantCulture));
		sb.Append(',');
		AppendField(sb, entry.Sink);
		sb.Append(',');
		AppendField(sb, entry.Message);
		return sb.ToString();
	}

	private static void AppendField(StringBuilder sb, string field) {
		if (field.AsSpan().IndexOfAny(s_mustQuote) < 0) {
			sb.Append(field);
			return;
		}

		sb.Append('"');
		foreach (var c in field)
			if (c == '"')
				sb.Append('"');

		sb.Append('"');
	}
}
