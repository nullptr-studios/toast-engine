using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Text;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets;

namespace editor.Components.Modals;

public partial class LoaderViewModel : ViewModelBase {
	private readonly List<LoaderTask> m_tasks;
	[ObservableProperty] private bool m_isRunning;
	[ObservableProperty] private double m_progress;

	public LoaderViewModel(IEnumerable<LoaderTask> tasks) {
		m_tasks = [..tasks];
	}

	// fake data for the designer
	public LoaderViewModel() {
		m_tasks = [];
		ConsoleLines.Add("> [1/3] Initializing project...");
		ConsoleLines.Add("> [2/3] Loading assets...");
		ConsoleLines.Add("warning: something minor happened");
		ConsoleLines.Add("> [3/3] Finalizing...");
		m_progress = 66;
	}

	public ObservableCollection<string> ConsoleLines { get; } = [];

	// set by the window before calling StartAsync
	public Func<Task>? OnComplete { get; set; }
	public Action? OnClose { get; set; }
	public Func<string, string, Task>? OnTaskError { get; set; }

	// called from the window's Opened event
	public async Task StartAsync() {
		IsRunning = true;
		var total = m_tasks.Count;

		for (var i = 0; i < total; i++) {
			var task = m_tasks[i];
			AppendLine($"> [{i + 1}/{total}] {task.Label}");

			try {
				await RunTaskAsync(task);
			} catch (Exception ex) {
				var failure = ex as LoaderTaskException ?? new LoaderTaskException(task.Label, ex.Message, ex);
				AppendLine($"error: {failure.Message}");
				if (OnTaskError is not null)
					await OnTaskError(failure.Title, failure.Message);
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
		switch (task) {
			case ActionTask a: await a.Action(AppendLine); break;
			case ProcessTask p: await RunProcessAsync(p.Exe, p.Args); break;
		}

		AppendLine("");
	}

	// streams stdout and stderr to the console lines as the process runs
	private Task RunProcessAsync(string exe, string args) {
		return Task.Run(() => {
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

			var stderr = new StringBuilder();
			var lastOut = "";

			proc.OutputDataReceived += (_, e) => {
				if (e.Data is null) return;
				if (!string.IsNullOrWhiteSpace(e.Data)) lastOut = e.Data;
				AppendLine(e.Data);
			};
			proc.ErrorDataReceived += (_, e) => {
				if (e.Data is null) return;
				stderr.AppendLine(e.Data);
				AppendLine(e.Data);
			};

			proc.Start();
			proc.BeginOutputReadLine();
			proc.BeginErrorReadLine();
			proc.WaitForExit();

			if (proc.ExitCode is 0) {
				AppendLine("exit code: 0 (success)");
				return;
			}

			// last log line + stderr, finished off with the exit code on its own line
			var detail = new StringBuilder();
			if (!string.IsNullOrWhiteSpace(lastOut)) detail.AppendLine(lastOut);
			var err = stderr.ToString().TrimEnd();
			if (err.Length > 0) detail.AppendLine(err);
			detail.Append($"(exited with code {proc.ExitCode})");

			throw new Exception(detail.ToString());
		});
	}

	// posts to the UI thread because this is called from background tasks and process callbacks
	private void AppendLine(string text) {
		Engine.Log.Trace(text);
		Dispatcher.UIThread.Post(() => ConsoleLines.Add(text));
	}
}
