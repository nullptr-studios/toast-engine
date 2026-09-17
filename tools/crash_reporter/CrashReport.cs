using System.Runtime.InteropServices;
using System.Text;

namespace crash_reporter;

internal sealed record StackFrameInfo(ulong Address, string Module, ulong ModuleOffset, string Function, ulong Displacement, string? File, uint Line);

internal sealed class CrashReport {
	public required string Reason { get; init; }
	public required string Message { get; init; }
	public required IReadOnlyList<StackFrameInfo> Frames { get; init; }
	public required string StackText { get; init; }

	public required string Text { get; init; }

	public string? Error { get; init; }

	public static unsafe CrashReport Build(CrashTarget target) {
		if (!target.IsOpen) {
			var reason = target.OpenError ?? "Could not open the crashed process";
			return new CrashReport { Reason = reason, Message = "", Frames = [], StackText = "", Text = reason, Error = reason };
		}

		var message = target.ReadMessage();

		// EXCEPTION_POINTERS { EXCEPTION_RECORD*, CONTEXT* }
		var recordAddress = target.ReadUInt64(target.Args.Pointers);
		var contextAddress = target.ReadUInt64(target.Args.Pointers + 8);

		var record = stackalloc byte[152];
		var haveRecord = target.Read(recordAddress, record, 152);
		var code = haveRecord ? *(uint*)record : 0;
		var parameterCount = haveRecord ? *(uint*)(record + 24) : 0;
		var parameters = (ulong*)(record + 32);

		var kindReason = target.Args.Kind switch {
			CrashKind.Exception => DescribeException(code, parameterCount >= 2 ? parameters[0] : 0, parameterCount >= 2 ? parameters[1] : 0),
			// The debug CRT reports abort() like an assertion before the signal is raised
			CrashKind.CrtReport when message.StartsWith("abort() has been called", StringComparison.Ordinal) => "abort() was called",
			CrashKind.CrtReport => "C runtime assertion failed",
			CrashKind.Terminate => "std::terminate() was called",
			CrashKind.PureCall => "Pure virtual function call",
			CrashKind.InvalidParameter => "Invalid parameter passed to a C runtime function",
			CrashKind.Abort => "abort() was called",
			_ => $"Crash of kind {(int)target.Args.Kind}",
		};

		var frames = StackWalker.Walk(target, contextAddress, out var walkError);

		// The context of a software crash was captured inside the handler so skip its frames
		if (target.Args.Kind != CrashKind.Exception) {
			var skip = 0;
			while (skip < frames.Count && frames[skip].Function.StartsWith("toast::crash::`anonymous namespace'", StringComparison.Ordinal)) ++skip;
			frames.RemoveRange(0, skip);
		}

		var culprit = frames.FirstOrDefault(f => f.Module != "?" && !RuntimeModules.Contains(f.Module) && !RuntimeFunctions.Contains(f.Function))
			?? frames.FirstOrDefault();
		var reasonText = culprit is not null ? $"{kindReason} in {FormatLocation(culprit)}" : kindReason;
		var stack = FormatStack(frames);

		var text = new StringBuilder()
			.AppendLine($"{target.ProcessName} (pid {target.Args.Pid}) crashed on thread {target.Args.Tid} at {DateTime.Now:yyyy-MM-dd HH:mm:ss}")
			.AppendLine(reasonText);
		if (message.Length > 0) text.AppendLine(message);
		if (walkError is not null) text.AppendLine($"Stack walk: {walkError}");
		text.AppendLine().AppendLine("Call stack:").Append(stack);

		return new CrashReport {
			Reason = reasonText,
			Message = message,
			Frames = frames,
			StackText = stack,
			Text = text.ToString(),
			Error = walkError,
		};
	}

	private static readonly HashSet<string> RuntimeModules = new(StringComparer.OrdinalIgnoreCase) {
		"ucrtbase.dll", "ucrtbased.dll", "vcruntime140.dll", "vcruntime140d.dll", "vcruntime140_1.dll", "vcruntime140_1d.dll",
		"msvcp140.dll", "msvcp140d.dll", "ntdll.dll", "kernelbase.dll", "kernel32.dll",
	};

	/// Statically linked into every module so the module filter cannot catch them
	private static readonly HashSet<string> RuntimeFunctions = new(StringComparer.Ordinal) { "__chkstk", "_chkstk", "__security_check_cookie" };

	private static string DescribeException(uint code, ulong info0, ulong info1) {
		return code switch {
			0xC0000005 => $"Access violation {(info0 switch { 1 => "writing", 8 => "executing", _ => "reading" })} 0x{info1:X16}",
			0xC00000FD => "Stack overflow",
			0xC000001D => "Illegal instruction",
			0xC0000096 => "Privileged instruction",
			0xC0000094 => "Integer division by zero",
			0xC000008C => "Array bounds exceeded",
			0xC0000006 => "In-page error",
			0x80000002 => "Datatype misalignment",
			0x80000003 => "Breakpoint",
			0xC0000374 => "Heap corruption",
			_ => $"Exception 0x{code:X8}",
		};
	}

	private static string FormatLocation(StackFrameInfo frame) {
		var function = frame.Function.Length > 0 ? $"{frame.Module}!{frame.Function}" : $"{frame.Module}+0x{frame.ModuleOffset:X}";
		return frame.File is null ? function : $"{function} ({frame.File}:{frame.Line})";
	}

	private static string FormatStack(IReadOnlyList<StackFrameInfo> frames) {
		var moduleWidth = frames.Count > 0 ? frames.Max(f => f.Module.Length) : 0;
		var builder = new StringBuilder();
		for (var i = 0; i < frames.Count; ++i) {
			var frame = frames[i];

			// Two copies show the recursion
			var run = 1;
			while (i + run < frames.Count && frames[i + run].Module == frame.Module && frames[i + run].Function == frame.Function &&
			       frames[i + run].Displacement == frame.Displacement) {
				++run;
			}
			if (run > 3 && frame.Function.Length > 0) {
				AppendFrame(builder, i, frame, moduleWidth);
				AppendFrame(builder, i + 1, frames[i + 1], moduleWidth);
				builder.AppendLine($"     ... the same frame {run - 2} more times (#{i + 2} to #{i + run - 1})");
				i += run - 1;
				continue;
			}

			AppendFrame(builder, i, frame, moduleWidth);
		}
		return builder.ToString();
	}

	private static void AppendFrame(StringBuilder builder, int index, StackFrameInfo frame, int moduleWidth) {
		if (frame.Module == "?") {
			builder.AppendLine($"#{index,-3} {"?".PadRight(moduleWidth)}  0x{frame.Address:X} - managed or unknown code, the native walk stops here");
			return;
		}

		var function = frame.Function.Length > 0 ? $"{frame.Function} + 0x{frame.Displacement:X}" : $"0x{frame.ModuleOffset:X}";
		builder.Append($"#{index,-3} {frame.Module.PadRight(moduleWidth)}  {function}");
		if (frame.File is not null) builder.Append($"    {frame.File}:{frame.Line}");
		builder.AppendLine();
	}
}

/// Not thread safe so everything runs on one worker thread
internal static unsafe class StackWalker {
	private const int ContextSize = 1232;    // x64 CONTEXT
	private const int FrameSize = 512;       // STACKFRAME64 is 264 bytes
	private const int SymbolInfoSize = 88;   // SYMBOL_INFOW without its name
	private const int MaxNameLength = 1024;

	public static List<StackFrameInfo> Walk(CrashTarget target, ulong contextAddress, out string? error) {
		error = null;
		var frames = new List<StackFrameInfo>();

		// StackWalk64 wants the x64 CONTEXT 16 byte aligned
		var context = (byte*)NativeMemory.AlignedAlloc(ContextSize, 16);
		try {
			if (!target.Read(contextAddress, context, ContextSize)) {
				error = "could not read the crashing thread's context";
				return frames;
			}

			Native.SymSetOptions(Native.SymOptions);
			if (!Native.SymInitializeW(target.ProcessHandle, null, true)) {
				error = $"SymInitialize failed (error {Marshal.GetLastPInvokeError()})";
				return frames;
			}

			try {
				var frame = stackalloc byte[FrameSize];
				new Span<byte>(frame, FrameSize).Clear();
				// ADDRESS64 is { Offset u64, Segment u16, Mode u32 at +12 }. AddrPC at 0 AddrFrame at 32 AddrStack at 48
				*(ulong*)(frame + 0) = *(ulong*)(context + 0xF8);     // Rip
				*(uint*)(frame + 12) = Native.AddrModeFlat;
				*(ulong*)(frame + 32) = *(ulong*)(context + 0xA0);    // Rbp
				*(uint*)(frame + 44) = Native.AddrModeFlat;
				*(ulong*)(frame + 48) = *(ulong*)(context + 0x98);    // Rsp
				*(uint*)(frame + 60) = Native.AddrModeFlat;

				var dbghelp = NativeLibrary.Load("dbghelp.dll");
				var functionTableAccess = NativeLibrary.GetExport(dbghelp, "SymFunctionTableAccess64");
				var getModuleBase = NativeLibrary.GetExport(dbghelp, "SymGetModuleBase64");

				for (var depth = 0; depth < 256; ++depth) {
					if (!Native.StackWalk64(Native.MachineAmd64, target.ProcessHandle, target.ThreadHandle, frame, context, IntPtr.Zero,
						    functionTableAccess, getModuleBase, IntPtr.Zero)) {
						break;
					}

					var pc = *(ulong*)frame;
					if (pc == 0) break;

					var described = Describe(target.ProcessHandle, pc, depth == 0);
					frames.Add(described);
					// No module means JIT compiled .NET code that dbghelp has no unwind data for
					if (described.Module == "?") break;
				}
			} finally {
				Native.SymCleanup(target.ProcessHandle);
			}
		} finally {
			NativeMemory.AlignedFree(context);
		}

		if (frames.Count == 0) error ??= "no frames could be walked";
		return frames;
	}

	private static StackFrameInfo Describe(IntPtr process, ulong pc, bool exact) {
		// A return address points after its call so one byte back is inside it
		var lookup = exact ? pc : pc - 1;

		var module = "?";
		var moduleBase = Native.SymGetModuleBase64(process, lookup);
		if (moduleBase != 0) {
			var name = stackalloc char[260];
			var length = Native.K32GetModuleBaseNameW(process, (IntPtr)(long)moduleBase, name, 260);
			if (length > 0) module = new string(name, 0, (int)length);
		}

		var function = "";
		ulong displacement = 0;
		var symbolBytes = SymbolInfoSize + MaxNameLength * sizeof(char);
		var symbol = stackalloc byte[symbolBytes];
		new Span<byte>(symbol, symbolBytes).Clear();
		*(uint*)symbol = SymbolInfoSize;                 // SizeOfStruct
		*(uint*)(symbol + 80) = MaxNameLength;           // MaxNameLen
		if (Native.SymFromAddrW(process, lookup, out displacement, symbol)) {
			var nameLength = Math.Min(*(uint*)(symbol + 76), MaxNameLength);    // NameLen
			function = new string((char*)(symbol + 84), 0, (int)nameLength);
		}

		string? file = null;
		uint line = 0;
		var lineInfo = stackalloc byte[40];    // IMAGEHLP_LINEW64
		new Span<byte>(lineInfo, 40).Clear();
		*(uint*)lineInfo = 40;
		if (Native.SymGetLineFromAddrW64(process, lookup, out _, lineInfo)) {
			var fileName = *(char**)(lineInfo + 24);
			if (fileName != null) file = new string(fileName);
			line = *(uint*)(lineInfo + 16);
		}

		return new StackFrameInfo(pc, module, moduleBase == 0 ? pc : pc - moduleBase, function, displacement, file, line);
	}
}
