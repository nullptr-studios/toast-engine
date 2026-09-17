using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace crash_reporter;

internal static unsafe class Native {
	public const uint ProcessAllAccess = 0x001F0FFF;
	public const uint ThreadAllAccess = 0x001FFFFF;
	public const uint MachineAmd64 = 0x8664;
	public const uint AddrModeFlat = 3;

	// SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS
	public const uint SymOptions = 0x00000002 | 0x00000004 | 0x00000010 | 0x00000200 | 0x00080000;

	// MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithUnloadedModules | MiniDumpWithFullMemoryInfo |
	// MiniDumpWithThreadInfo
	public const uint FullDumpType = 0x00000002 | 0x00000004 | 0x00000020 | 0x00000800 | 0x00001000;

	[DllImport("kernel32.dll", SetLastError = true)]
	public static extern IntPtr OpenProcess(uint access, bool inherit, uint processId);

	[DllImport("kernel32.dll", SetLastError = true)]
	public static extern IntPtr OpenThread(uint access, bool inherit, uint threadId);

	[DllImport("kernel32.dll", SetLastError = true)]
	public static extern bool CloseHandle(IntPtr handle);

	[DllImport("kernel32.dll", SetLastError = true)]
	public static extern bool ReadProcessMemory(IntPtr process, ulong address, byte* buffer, nuint size, out nuint read);

	[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	public static extern uint K32GetModuleBaseNameW(IntPtr process, IntPtr module, char* name, uint size);

	[DllImport("ntdll.dll")]
	public static extern int NtSuspendProcess(IntPtr process);

	[DllImport("ntdll.dll")]
	public static extern int NtResumeProcess(IntPtr process);

	[DllImport("dbghelp.dll", SetLastError = true)]
	public static extern uint SymSetOptions(uint options);

	[DllImport("dbghelp.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	public static extern bool SymInitializeW(IntPtr process, string? searchPath, bool invadeProcess);

	[DllImport("dbghelp.dll", SetLastError = true)]
	public static extern bool SymCleanup(IntPtr process);

	[DllImport("dbghelp.dll", SetLastError = true)]
	public static extern bool StackWalk64(
		uint machineType, IntPtr process, IntPtr thread, byte* stackFrame, byte* context, IntPtr readMemory,
		IntPtr functionTableAccess, IntPtr getModuleBase, IntPtr translateAddress
	);

	[DllImport("dbghelp.dll", SetLastError = true)]
	public static extern ulong SymGetModuleBase64(IntPtr process, ulong address);

	[DllImport("dbghelp.dll", SetLastError = true)]
	public static extern bool SymFromAddrW(IntPtr process, ulong address, out ulong displacement, byte* symbol);

	[DllImport("dbghelp.dll", SetLastError = true)]
	public static extern bool SymGetLineFromAddrW64(IntPtr process, ulong address, out uint displacement, byte* line);

	[DllImport("dbghelp.dll", SetLastError = true)]
	public static extern bool MiniDumpWriteDump(
		IntPtr process, uint processId, SafeFileHandle file, uint dumpType, MinidumpExceptionInformation* exception,
		IntPtr userStreams, IntPtr callback
	);

	/// MINIDUMP_EXCEPTION_INFORMATION is declared under pshpack4
	[StructLayout(LayoutKind.Sequential, Pack = 4)]
	public struct MinidumpExceptionInformation {
		public uint ThreadId;
		public IntPtr ExceptionPointers;
		public int ClientPointers;
	}
}
