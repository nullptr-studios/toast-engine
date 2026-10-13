using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Threading;

namespace editor.Git;

/// <summary>
/// Answers git and ssh credential prompts with an editor modal. The editor executable doubles as the askpass
/// program and forwards each prompt to the running editor over a per session named pipe
/// </summary>
public static class GitAskpass {
	private const string k_pipeVariable = "TOAST_ASKPASS_PIPE";
	private const string k_runVariable = "TOAST_ASKPASS_RUN";

	private static readonly UTF8Encoding s_utf8 = new(false);
	private static readonly string s_pipeName = $"toast-askpass-{Environment.ProcessId}-{Guid.NewGuid():N}";
	private static readonly Lazy<string?> s_helper = new(ResolveHelper);

	// Everything below is guarded by s_lock
	private static readonly object s_lock = new();
	private static readonly Dictionary<string, string> s_answers = new();
	private static readonly Dictionary<string, Task<string?>> s_pending = new();
	private static readonly HashSet<string> s_declined = new();
	private static readonly Dictionary<string, Run> s_runs = new();
	private static bool s_serving;
	private static long s_nextRun;

	private sealed class Run(CancellationTokenSource timeout, TimeSpan duration) {
		public readonly Dictionary<string, int> Asked = new();
		private int m_waiting;

		// The git timeout must not run out while the user types
		public void Pause() {
			if (m_waiting++ == 0) timeout.CancelAfter(Timeout.Infinite);
		}

		public void Resume() {
			if (--m_waiting == 0) timeout.CancelAfter(duration);
		}
	}

	public static bool IsHelperInvocation => Environment.GetEnvironmentVariable(k_pipeVariable) is { Length: > 0 };

	/// <summary>
	/// Wires askpass into a git process and returns the run id to hand back to EndRun, null when unavailable
	/// </summary>
	public static string? BeginRun(ProcessStartInfo psi, CancellationTokenSource timeout, TimeSpan duration) {
		if (s_helper.Value is not { } helper) return null;

		string id;
		lock (s_lock) {
			if (!s_serving) {
				s_serving = true;
				_ = Task.Run(ServeAsync);
			}

			id = (++s_nextRun).ToString();
			s_runs[id] = new Run(timeout, duration);
		}

		psi.Environment["SSH_ASKPASS"] = helper;
		psi.Environment["SSH_ASKPASS_REQUIRE"] = "force";
		psi.Environment["GIT_ASKPASS"] = helper;
		psi.Environment[k_pipeVariable] = s_pipeName;
		psi.Environment[k_runVariable] = id;
		return id;
	}

	public static void EndRun(string? id) {
		if (id is null) return;
		lock (s_lock) s_runs.Remove(id);
	}

	/// <summary>
	/// Lets prompts the user cancelled show again. Called by user initiated operations so background refreshes
	/// never nag after a cancel
	/// </summary>
	public static void AllowDeclinedPrompts() {
		lock (s_lock) s_declined.Clear();
	}

	public static int RunHelper(string[] args) {
		// ssh only wants an acknowledgement for informational prompts
		if (Environment.GetEnvironmentVariable("SSH_ASKPASS_PROMPT") == "none") return 0;

		var prompt = string.Join(' ', args).ReplaceLineEndings(" ").Trim();
		try {
			using var pipe = new NamedPipeClientStream(".", Environment.GetEnvironmentVariable(k_pipeVariable)!,
				PipeDirection.InOut, PipeOptions.CurrentUserOnly);
			pipe.Connect(5000);

			using var writer = new StreamWriter(pipe, s_utf8, leaveOpen: true) { NewLine = "\n", AutoFlush = true };
			writer.WriteLine(Environment.GetEnvironmentVariable(k_runVariable) ?? "");
			writer.WriteLine(FindSshProcess());
			writer.WriteLine(prompt);

			using var reader = new StreamReader(pipe, s_utf8, leaveOpen: true);
			var status = reader.ReadLine();
			if (status != "1") return Fail(status is null ? "the editor closed the pipe without answering" : "cancelled");

			using var stdout = Console.OpenStandardOutput();
			stdout.Write(s_utf8.GetBytes((reader.ReadLine() ?? "") + "\n"));
			stdout.Flush();
			return 0;
		} catch (Exception e) {
			return Fail(e.Message);
		}
	}

	// ssh retries a rejected passphrase from the same process. MSYS ssh forks before launching askpass so the
	// fork sits between us and the ssh that owns the connection
	private static int FindSshProcess() {
		try {
			var tree = OperatingSystem.IsWindows() ? WindowsProcessTree() : ProcProcessTree();
			var ssh = 0;
			if (!tree.TryGetValue(Environment.ProcessId, out var self)) return 0;

			var pid = self.Parent;
			for (var depth = 0; depth < 8 && tree.TryGetValue(pid, out var entry) && entry.Name == "ssh"; depth++) {
				ssh = pid;
				pid = entry.Parent;
			}

			return ssh;
		} catch {
			return 0;
		}
	}

	private static Dictionary<int, (int Parent, string Name)> ProcProcessTree() {
		var tree = new Dictionary<int, (int, string)>();
		foreach (var dir in Directory.EnumerateDirectories("/proc")) {
			if (!int.TryParse(Path.GetFileName(dir), out var pid)) continue;
			try {
				// pid (comm) state ppid
				var stat = File.ReadAllText(Path.Combine(dir, "stat"));
				var open = stat.IndexOf('(');
				var close = stat.LastIndexOf(')');
				var rest = stat[(close + 2)..].Split(' ');
				tree[pid] = (int.Parse(rest[1]), stat[(open + 1)..close]);
			} catch (IOException) {
				// exited while listing
			}
		}

		return tree;
	}

	private static Dictionary<int, (int Parent, string Name)> WindowsProcessTree() {
		var tree = new Dictionary<int, (int, string)>();
		var snapshot = CreateToolhelp32Snapshot(k_snapProcess, 0);
		if (snapshot == new IntPtr(-1)) return tree;

		try {
			var entry = new ProcessEntry32 { Size = (uint)Marshal.SizeOf<ProcessEntry32>() };
			for (var ok = Process32FirstW(snapshot, ref entry); ok; ok = Process32NextW(snapshot, ref entry)) {
				var name = Path.GetFileNameWithoutExtension(entry.ExeFile).ToLowerInvariant();
				tree[(int)entry.ProcessId] = ((int)entry.ParentProcessId, name);
			}
		} finally {
			CloseHandle(snapshot);
		}

		return tree;
	}

	private const uint k_snapProcess = 0x2;

	// Mirrors PROCESSENTRY32W
	[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
	private struct ProcessEntry32 {
		public uint Size;
		public uint Usage;
		public uint ProcessId;
		public IntPtr DefaultHeapId;
		public uint ModuleId;
		public uint Threads;
		public uint ParentProcessId;
		public int PriorityClassBase;
		public uint Flags;

		[MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
		public string ExeFile;
	}

	[DllImport("kernel32.dll", SetLastError = true)]
	private static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint processId);

	[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
	private static extern bool Process32FirstW(IntPtr snapshot, ref ProcessEntry32 entry);

	[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
	private static extern bool Process32NextW(IntPtr snapshot, ref ProcessEntry32 entry);

	[DllImport("kernel32.dll")]
	private static extern bool CloseHandle(IntPtr handle);

	// ssh forwards askpass stderr into the git error the editor logs
	private static int Fail(string reason) {
		try {
			using var stderr = Console.OpenStandardError();
			stderr.Write(s_utf8.GetBytes($"toast askpass {reason}\n"));
		} catch (IOException) {
		}

		return 1;
	}

	private static string? ResolveHelper() {
		var path = Environment.ProcessPath;
		if (path is null) return null;

		// Under dotnet exec the apphost next to the assembly is the program to launch
		if (Path.GetFileNameWithoutExtension(path).Equals("dotnet", StringComparison.OrdinalIgnoreCase)) {
			path = Path.ChangeExtension(typeof(GitAskpass).Assembly.Location, OperatingSystem.IsWindows() ? ".exe" : null);
			if (!File.Exists(path)) return null;
		}

		// Both MSYS ssh and native programs take forward slashes
		return path.Replace('\\', '/');
	}

	private static async Task ServeAsync() {
		while (true) {
			var pipe = new NamedPipeServerStream(s_pipeName, PipeDirection.InOut,
				NamedPipeServerStream.MaxAllowedServerInstances, PipeTransmissionMode.Byte,
				PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
			try {
				await pipe.WaitForConnectionAsync().ConfigureAwait(false);
			} catch (Exception) {
				await pipe.DisposeAsync().ConfigureAwait(false);
				await Task.Delay(1000).ConfigureAwait(false);
				continue;
			}

			_ = HandleAsync(pipe);
		}
	}

	private static async Task HandleAsync(NamedPipeServerStream pipe) {
		await using (pipe) {
			try {
				using var reader = new StreamReader(pipe, s_utf8, leaveOpen: true);
				var run = await reader.ReadLineAsync().ConfigureAwait(false) ?? "";
				var ssh = await reader.ReadLineAsync().ConfigureAwait(false) ?? "0";
				var prompt = await reader.ReadLineAsync().ConfigureAwait(false) ?? "";
				var answer = await AnswerAsync(run, ssh, prompt).ConfigureAwait(false);

				await using var writer = new StreamWriter(pipe, s_utf8, leaveOpen: true) { NewLine = "\n" };
				await writer.WriteLineAsync(answer is null ? "0" : "1").ConfigureAwait(false);
				if (answer is not null) await writer.WriteLineAsync(answer).ConfigureAwait(false);
				await writer.FlushAsync().ConfigureAwait(false);

				// Closing the server end first can drop the reply before the helper reads it on Windows
				using var drain = new CancellationTokenSource(TimeSpan.FromSeconds(5));
				await reader.ReadLineAsync(drain.Token).ConfigureAwait(false);
			} catch (OperationCanceledException) {
			} catch (IOException) {
				// git was killed while the modal was open
			}
		}
	}

	private static async Task<string?> AnswerAsync(string runId, string ssh, string prompt) {
		Run? run;
		Task<string?> pending;
		lock (s_lock) {
			run = s_runs.GetValueOrDefault(runId);
			var asked = 0;
			if (run is not null && ssh != "0") {
				var key = ssh + "\n" + prompt;
				asked = run.Asked.GetValueOrDefault(key);
				run.Asked[key] = asked + 1;
			}

			// The same ssh process asking again means the previous answer was rejected while one git call
			// may open several ssh connections that each ask once
			var rejected = asked > 0;
			if (rejected) s_answers.Remove(prompt);
			else if (s_answers.TryGetValue(prompt, out var cached)) return cached;
			else if (s_declined.Contains(prompt)) return null;

			if (!s_pending.TryGetValue(prompt, out var existing)) {
				existing = Dispatcher.UIThread.InvokeAsync(() => GitPassphraseModal.ShowAsync(prompt, rejected));
				s_pending[prompt] = existing;
			}

			pending = existing;
			run?.Pause();
		}

		string? answer = null;
		try {
			answer = await pending.ConfigureAwait(false);
		} finally {
			lock (s_lock) {
				if (s_pending.GetValueOrDefault(prompt) == pending) {
					s_pending.Remove(prompt);
					if (answer is null) s_declined.Add(prompt);
					else s_answers[prompt] = answer;
				}

				if (run is not null && s_runs.ContainsKey(runId)) run.Resume();
			}
		}

		return answer;
	}
}
