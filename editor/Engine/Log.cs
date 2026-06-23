using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace editor.Engine;

public static partial class Log {
	public static void Trace(string message, [CallerFilePath] string fileName = "", [CallerLineNumber] int lineNumber = 0) {
		Dispatch(toast_trace, message, fileName, (uint)lineNumber);
	}

	public static void Info(string message, [CallerFilePath] string fileName = "", [CallerLineNumber] int lineNumber = 0) {
		Dispatch(toast_info, message, fileName, (uint)lineNumber);
	}

	public static void Warn(string message, [CallerFilePath] string fileName = "", [CallerLineNumber] int lineNumber = 0) {
		Dispatch(toast_warn, message, fileName, (uint)lineNumber);
	}

	public static void Error(string message, [CallerFilePath] string fileName = "", [CallerLineNumber] int lineNumber = 0) {
		Dispatch(toast_error, message, fileName, (uint)lineNumber);
	}

	public static void Critical(string message, [CallerFilePath] string fileName = "", [CallerLineNumber] int lineNumber = 0) {
		Dispatch(toast_critical, message, fileName, (uint)lineNumber);
	}

	// Fallback to the console if toast-engine is not loaded
	private static void Dispatch(Action<string, string, string, uint> sink, string message, string fileName, uint lineNumber) {
		try {
			sink("Editor", message, fileName, lineNumber);
		} catch (DllNotFoundException) {
			Console.WriteLine(message);
		}
	}

	// Engine shit
	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_trace(string sink, string message, string fileName, uint lineNumber);

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_info(string sink, string message, string fileName, uint lineNumber);

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_warn(string sink, string message, string fileName, uint lineNumber);

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_error(string sink, string message, string fileName, uint lineNumber);

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_critical(string sink, string message, string fileName, uint lineNumber);
}
