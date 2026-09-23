using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace crash_reporter;

internal enum CrashKind {
	Exception = 0,
	CrtReport = 1,
	Terminate = 2,
	PureCall = 3,
	InvalidParameter = 4,
	Abort = 5,
}

internal sealed class CrashArgs {
	public required uint Pid { get; init; }
	public required uint Tid { get; init; }
	public required ulong Pointers { get; init; }
	public required ulong Message { get; init; }
	public required CrashKind Kind { get; init; }
	public string? DumpDir { get; init; }
	public static CrashArgs? Parse(string[] args) {
		uint? pid = null, tid = null;
		ulong? pointers = null, message = null;
		CrashKind? kind = null;
		string? dumpDir = null;

		for (var i = 0; i < args.Length - 1; ++i) {
			switch (args[i]) {
				case "--pid": pid = uint.Parse(args[++i], CultureInfo.InvariantCulture); break;
				case "--tid": tid = uint.Parse(args[++i], CultureInfo.InvariantCulture); break;
				case "--pointers": pointers = ParseAddress(args[++i]); break;
				case "--message": message = ParseAddress(args[++i]); break;
				case "--kind": kind = (CrashKind)int.Parse(args[++i], CultureInfo.InvariantCulture); break;
				case "--dump-dir": dumpDir = args[++i]; break;
			}
		}

		if (pid is null || tid is null || pointers is null || message is null || kind is null) return null;
		return new CrashArgs { Pid = pid.Value, Tid = tid.Value, Pointers = pointers.Value, Message = message.Value, Kind = kind.Value, DumpDir = dumpDir };
	}

	private static ulong ParseAddress(string text) {
		return text.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
			? ulong.Parse(text.AsSpan(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture)
			: ulong.Parse(text, CultureInfo.InvariantCulture);
	}
}

internal sealed unsafe class CrashTarget : IDisposable {
	public required CrashArgs Args { get; init; }
	public bool IsOpen { get; private init; }
	public string? OpenError { get; private init; }
	public string ProcessName { get; private init; } = "";
	public IntPtr ProcessHandle { get; private init; }
	public IntPtr ThreadHandle { get; private init; }

	private CrashTarget() { }

	public static CrashTarget Open(CrashArgs args) {
		var process = Native.OpenProcess(Native.ProcessAllAccess, false, args.Pid);
		if (process == IntPtr.Zero) {
			return new CrashTarget { Args = args, IsOpen = false, OpenError = $"Could not open process {args.Pid} (error {Marshal.GetLastWin32Error()})" };
		}

		var thread = Native.OpenThread(Native.ThreadAllAccess, false, args.Tid);
		if (thread == IntPtr.Zero) {
			var error = $"Could not open thread {args.Tid} (error {Marshal.GetLastWin32Error()})";
			Native.CloseHandle(process);
			return new CrashTarget { Args = args, IsOpen = false, OpenError = error };
		}

		Native.NtSuspendProcess(process);

		var name = stackalloc char[260];
		var length = Native.K32GetModuleBaseNameW(process, IntPtr.Zero, name, 260);
		var processName = length > 0 ? new string(name, 0, (int)length) : $"pid {args.Pid}";

		return new CrashTarget { Args = args, IsOpen = true, ProcessName = processName, ProcessHandle = process, ThreadHandle = thread };
	}

	public bool Read(ulong address, byte* buffer, int size) {
		return IsOpen && Native.ReadProcessMemory(ProcessHandle, address, buffer, (nuint)size, out _);
	}

	public ulong ReadUInt64(ulong address) {
		ulong value = 0;
		Read(address, (byte*)&value, sizeof(ulong));
		return value;
	}

	public string ReadMessage() {
		if (!IsOpen) return "";

		var buffer = stackalloc byte[4096];
		if (!Read(Args.Message, buffer, 4096)) return "";

		var length = 0;
		while (length < 4096 && buffer[length] != 0) ++length;
		return Encoding.UTF8.GetString(buffer, length);
	}

	public string WriteFullDump() {
		if (!IsOpen) throw new InvalidOperationException("The crashed process could not be opened");

		var directory = string.IsNullOrEmpty(Args.DumpDir) ? Path.Combine(Path.GetTempPath(), "toast_crashes") : Args.DumpDir;
		Directory.CreateDirectory(directory);
		var path = Path.Combine(directory, $"{Path.GetFileNameWithoutExtension(ProcessName)}_{Args.Pid}_{DateTime.Now:yyyyMMdd_HHmmss}.dmp");

		using var file = File.OpenHandle(path, FileMode.Create, FileAccess.Write, FileShare.None);
		var exceptionInfo = new Native.MinidumpExceptionInformation {
			ThreadId = Args.Tid,
			ExceptionPointers = (IntPtr)Args.Pointers,
			ClientPointers = 1,
		};

		if (!Native.MiniDumpWriteDump(ProcessHandle, Args.Pid, file, Native.FullDumpType, &exceptionInfo, IntPtr.Zero, IntPtr.Zero)) {
			throw new IOException($"MiniDumpWriteDump failed (error {Marshal.GetLastWin32Error()})");
		}

		return path;
	}

	public void Dispose() {
		if (!IsOpen) return;
		Native.NtResumeProcess(ProcessHandle);
		Native.CloseHandle(ThreadHandle);
		Native.CloseHandle(ProcessHandle);
	}
}
