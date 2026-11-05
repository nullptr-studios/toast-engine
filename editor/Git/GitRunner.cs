using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace editor.Git;

public static class GitRunner {
	// Anything that touches the index goes through this gate
	private static readonly SemaphoreSlim s_indexGate = new(1, 1);

	// linux doesnt have a GCR
	private static readonly Lazy<bool> s_needsCredentialHelper = new(() => {
		if (OperatingSystem.IsWindows()) return false;
		try {
			var psi = new ProcessStartInfo("git") {
				UseShellExecute = false,
				CreateNoWindow = true,
				RedirectStandardOutput = true,
				RedirectStandardError = true
			};
			psi.ArgumentList.Add("config");
			psi.ArgumentList.Add("--get-all");
			psi.ArgumentList.Add("credential.helper");
			using var process = Process.Start(psi);
			if (process is null) return false;
			var output = process.StandardOutput.ReadToEnd();
			process.WaitForExit(5000);
			return string.IsNullOrWhiteSpace(output);
		} catch {
			return false;
		}
	});

	public static async Task<GitResult> RunAsync(
		string workingDirectory,
		IEnumerable<string> args,
		string? stdin = null,
		bool touchesIndex = true,
		TimeSpan? timeout = null,
		CancellationToken ct = default) {
		if (touchesIndex) await s_indexGate.WaitAsync(ct).ConfigureAwait(false);
		try {
			return await RunCore(workingDirectory, args, stdin, timeout ?? TimeSpan.FromMinutes(2), ct)
				.ConfigureAwait(false);
		} finally {
			if (touchesIndex) s_indexGate.Release();
		}
	}

	private static async Task<GitResult> RunCore(string workingDirectory, IEnumerable<string> args, string? stdin,
		TimeSpan timeout, CancellationToken ct) {
		var psi = new ProcessStartInfo {
			FileName = "git",
			WorkingDirectory = workingDirectory,
			UseShellExecute = false,
			CreateNoWindow = true,
			RedirectStandardOutput = true,
			RedirectStandardError = true,
			RedirectStandardInput = stdin is not null,
			StandardOutputEncoding = Encoding.UTF8,
			StandardErrorEncoding = Encoding.UTF8
		};
		// Only valid when stdin is redirected
		if (stdin is not null) psi.StandardInputEncoding = new UTF8Encoding(false);
		// windows does have a GCR tho
		if (s_needsCredentialHelper.Value) {
			psi.ArgumentList.Add("-c");
			psi.ArgumentList.Add("credential.helper=store");
		}

		foreach (var arg in args) psi.ArgumentList.Add(arg);

		// Credential prompts go to the editor through askpass and never to a terminal
		psi.Environment["GIT_TERMINAL_PROMPT"] = "0";
		psi.Environment["GCM_INTERACTIVE"] = "never";
		psi.Environment["GIT_OPTIONAL_LOCKS"] = "0";
		psi.Environment["GIT_LFS_FORCE_PROGRESS"] = "0";

		using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
		cts.CancelAfter(timeout);
		var askpass = GitAskpass.BeginRun(psi, cts, timeout);
		try {
			Process process;
			try {
				process = Process.Start(psi) ?? throw new InvalidOperationException("git did not start");
			} catch (Exception e) {
				return new GitResult(-1, "", e.Message);
			}

			using (process) {
				try {
					if (stdin is not null) {
						await process.StandardInput.WriteAsync(stdin).ConfigureAwait(false);
						process.StandardInput.Close();
					}

					var stdout = process.StandardOutput.ReadToEndAsync(cts.Token);
					var stderr = process.StandardError.ReadToEndAsync(cts.Token);
					await process.WaitForExitAsync(cts.Token).ConfigureAwait(false);
					return new GitResult(process.ExitCode, await stdout.ConfigureAwait(false),
						await stderr.ConfigureAwait(false));
				} catch (OperationCanceledException) {
					try {
						process.Kill(true);
					} catch {
						// already gone
					}

					return new GitResult(-1, "", ct.IsCancellationRequested ? "cancelled" : "git timed out");
				} catch (Exception e) {
					return new GitResult(-1, "", e.Message);
				}
			}
		} finally {
			GitAskpass.EndRun(askpass);
		}
	}
}
