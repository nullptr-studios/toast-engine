using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Services;

namespace editor.Loader;

public partial class LoaderViewModel : ViewModelBase {
	private readonly List<LoaderTask> m_tasks;
	[ObservableProperty] private bool m_isRunning;

	[ObservableProperty] private double m_progress;

	public LoaderViewModel(IEnumerable<LoaderTask> tasks) {
		m_tasks = [..tasks];
	}

	public LoaderViewModel() {
		m_tasks = [];
		ConsoleLines.Add("> [1/3] Initializing project...");
		ConsoleLines.Add("> [2/3] Loading assets...");
		ConsoleLines.Add("warning: something minor happened");
		ConsoleLines.Add("> [3/3] Finalizing...");
		m_progress = 66;
	}

	public ObservableCollection<string> ConsoleLines { get; } = [];

	// Set by the window before calling StartAsync
	public Func<Task>? OnComplete { get; set; }
	public Action? OnClose { get; set; }

	// Called from the window's Opened event
	public async Task StartAsync() {
		IsRunning = true;
		var total = m_tasks.Count;

		for (var i = 0; i < total; i++) {
			var task = m_tasks[i];
			AppendLine($"> [{i + 1}/{total}] {task.Label}");

			try {
				await RunTaskAsync(task);
			} catch (Exception ex) {
				AppendLine($"error: {ex.Message}");
			}

			Progress = (double)(i + 1) / total * 100.0;
		}

		if (OnComplete is not null)
			try {
				await OnComplete();
			} catch (Exception ex) {
				AppendLine($"error during completion: {ex.Message}");
			}

		IsRunning = false;
		OnClose?.Invoke();
	}

	private async Task RunTaskAsync(LoaderTask task) {
		if (task.Action is not null)
			// Function tasks write to the console in real time via the log callback
			await task.Action(line => AppendLine(line));
		else if (task.Exe is not null) await RunProcessAsync(task.Exe, task.Args ?? "");
		AppendLine("");
	}

	// Streams stdout/stderr as the process runs
	private Task RunProcessAsync(string exe, string args) {
		return Task.Run(() => {
			try {
				using var proc = new Process();
				proc.StartInfo = new ProcessStartInfo {
					FileName = exe,
					Arguments = args,
					WorkingDirectory = ProjectContext.IsInitialized ? ProjectContext.ProjectPath : "",
					RedirectStandardOutput = true,
					RedirectStandardError = true,
					UseShellExecute = false,
					CreateNoWindow = true
				};

				proc.OutputDataReceived += (_, e) => {
					if (e.Data is not null) AppendLine(e.Data);
				};
				proc.ErrorDataReceived += (_, e) => {
					if (e.Data is not null) AppendLine(e.Data);
				};

				proc.Start();
				proc.BeginOutputReadLine();
				proc.BeginErrorReadLine();
				proc.WaitForExit();

				if (proc.ExitCode != 0)
					AppendLine($"(exited with code {proc.ExitCode})");
			} catch (Exception ex) {
				AppendLine($"error: {ex.Message}");
			}
		});
	}

	private void AppendLine(string text) {
		Dispatcher.UIThread.Post(() => ConsoleLines.Add(text));
	}
}
