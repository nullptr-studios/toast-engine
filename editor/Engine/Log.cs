using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace editor.Engine;

public static partial class Log {
	static void Trace(string message, [CallerFilePath] string fileName = "", [CallerLineNumber] int lineNumber = 0) {
		toast_trace("Editor", message, fileName, (uint)lineNumber);
	}

	static void Info(string message, [CallerFilePath] string fileName = "", [CallerLineNumber] int lineNumber = 0) {
		toast_info("Editor", message, fileName, (uint)lineNumber);
	}

	static void Warn(string message, [CallerFilePath] string fileName = "", [CallerLineNumber] int lineNumber = 0) {
		toast_warn("Editor", message, fileName, (uint)lineNumber);
	}

	static void Error(string message, [CallerFilePath] string fileName = "", [CallerLineNumber] int lineNumber = 0) {
		toast_error("Editor", message, fileName, (uint)lineNumber);
	}

	static void Critical(string message, [CallerFilePath] string fileName = "", [CallerLineNumber] int lineNumber = 0) {
		toast_critical("Editor", message, fileName, (uint)lineNumber);
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
