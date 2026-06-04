using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using editor.Workspace;
using Avalonia.Threading;

namespace editor.Loader;

public partial class SplashWindow : Window {
	private record SplashTask(string Label, string? Exe = null, string? Args = null, Func<Task<string>>? Action = null);

	private static readonly string ToastPath =
		Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "../toast_engine"));

	private List<SplashTask> m_tasks = [
		new SplashTask("git init", Exe: "git", Args: "init"),
		new SplashTask("git add .", Exe: "git", Args: "add ."),
		new SplashTask("git commit -m \"Initial commit\"", Exe: "git",
			Args: "commit -m \"Initial commit\" --author \"nullptr Studios <toast-engine@nullptr.es>\""),
		new SplashTask("cmake lib/ -B .toast/cmake_cache -G \"Visual Studio 18 2026", Exe: "cmake",
			Args: $"lib/ -B .toast/cmake_cache -G \"Visual Studio 18 2026\" -DTOAST_PATH={ToastPath}"),
		new SplashTask("cmake --build .toast/cmake_cache", Exe: "cmake", Args: "--build .toast/cmake_cache"),
		// TODO: new SplashTask("cache game.dll", action: );
	];

	private static string? m_project;

	public SplashWindow(string projectPath) {
		InitializeComponent();
		m_project = projectPath;

		if (Directory.Exists(Path.Combine(Path.GetDirectoryName(m_project)!, ".git"))) {
			m_tasks.RemoveRange(0, 3);
		}

		Opened += async (_, _) => await RunCommandsAsync();
	}

	protected override void OnOpened(EventArgs e) {
		base.OnOpened(e);
		SetSplashImage();
	}

	private void SetSplashImage() {
		try {
			var folderPath = Path.Combine(AppContext.BaseDirectory, "Resources/splash_images");
			if (!Directory.Exists(folderPath)) return;

			var files = Directory.GetFiles(folderPath).Where(f => f.EndsWith(".jpg", StringComparison.OrdinalIgnoreCase))
				.ToArray();
			if (files.Length <= 0) return;

			var random = new Random();
			var randomImage = files[random.Next(files.Length)];
			SplashImage.Source = new Bitmap(randomImage);
		} catch (Exception e) {
			Debug.WriteLine($"Error setting splash image: {e.Message}");
			throw;
		}
	}

	private async Task RunCommandsAsync() {
		int total = m_tasks.Count;
		for (int i = 0; i < total; i++) {
			var task = m_tasks[i];
			int stepNumber = i + 1;

			AppendLine($"> [{stepNumber}/{total}] {task.Label}");
			string output = task.Action is not null
				? await task.Action()
				: await RunProcessAsync(task.Exe!, task.Args ?? "");

			foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries)) {
				AppendLine(line.TrimEnd());
			}

			AppendLine("");

			double progress = (double)stepNumber / total * 100.0;
			await Dispatcher.UIThread.InvokeAsync(() => ProgressBar.Value = progress);

			await Task.Delay(120);
		}

		if (m_project != null) {
			_ = new ToastEngine(m_project);
		}
		
		Close();
	}

	private void AppendLine(string text) {
		Dispatcher.UIThread.Post(() => {
			ConsoleOutput.Text += text + '\n';
			ConsoleScroll.ScrollToEnd();
		});
	}

	private Task<string> RunProcessAsync(string exe, string args) {
		return Task.Run(() => {
			try {
				using var proc = new Process();
				proc.StartInfo = new ProcessStartInfo {
					FileName = exe,
					Arguments = args,
					WorkingDirectory = Path.GetDirectoryName(m_project),
					RedirectStandardOutput = true,
					RedirectStandardError = true,
					UseShellExecute = false,
					CreateNoWindow = true
				};

				var sb = new StringBuilder();
				proc.OutputDataReceived += (_, e) => {
					if (e.Data is not null) sb.AppendLine(e.Data);
				};
				proc.ErrorDataReceived += (_, e) => {
					if (e.Data is not null) sb.AppendLine(e.Data);
				};

				proc.Start();
				proc.BeginOutputReadLine();
				proc.BeginErrorReadLine();
				proc.WaitForExit();

				return sb.Length > 0 ? sb.ToString() : "(No output)";
			} catch (Exception e) {
				return $"error: {e.Message}";
			}
		});
	}
}
