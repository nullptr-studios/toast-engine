using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using editor.MainWindow;

namespace editor.Loader;


public partial class SplashWindow : Window
{
	private record SplashTask(string label, string? exe = null, string? args = null, Func<Task<string>>? action = null);
	private static readonly string toast_path = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "../toast_engine"));
	private List<SplashTask> m_tasks = [
		new SplashTask("git init", exe: "git", args: "init"),
		new SplashTask("git add .", exe: "git", args: "add ."),
		new SplashTask("git commit -m \"Initial commit\"", exe: "git", args: "commit -m \"Initial commit\" --author \"nullptr Studios <toast-engine@nullptr.es>\""),
		new SplashTask("cmake lib/ -B .toast/cmake_cache -G \"Visual Studio 18 2026", exe: "cmake",
			args: $"lib/ -B .toast/cmake_cache -G \"Visual Studio 18 2026\" -DTOAST_PATH={toast_path}"),
		new SplashTask("cmake --build .toast/cmake_cache", exe: "cmake", args: "--build .toast/cmake_cache"),
		// TODO: new SplashTask("cache game.dll", action: );
	];

	private static string m_project;

	public SplashWindow(string project_path)
	{
		InitializeComponent();
		m_project = project_path;

		if (Directory.Exists(Path.Combine(Path.GetDirectoryName(m_project)!, ".git")))
		{
			m_tasks.RemoveRange(0, 3);
		}

		Opened += async (_, __) => await runCommandsAsync();
	}

	protected override void OnOpened(EventArgs e)
	{
		base.OnOpened(e);
		setSplashImage();
	}

	private void setSplashImage()
	{
		try
		{
			var folder_path = Path.Combine(AppContext.BaseDirectory, "Resources/splash_images");
			if (!Directory.Exists(folder_path)) return;

			var files = Directory.GetFiles(folder_path).Where(f => f.EndsWith(".jpg", StringComparison.OrdinalIgnoreCase)).ToArray();
			if (files.Length <= 0) return;

			var random = new Random();
			var random_image = files[random.Next(files.Length)];
			SplashImage.Source = new Bitmap(random_image);

		}
		catch (Exception e)
		{
			Debug.WriteLine($"Error setting splash image: {e.Message}");
			throw;
		}
	}

	private async Task runCommandsAsync()
	{
		int total = m_tasks.Count;
		for (int i = 0; i < total; i++)
		{
			var task = m_tasks[i];
			int step_number = i + 1;

			appendLine($"> [{step_number}/{total}] {task.label}", "#00d4ff");
			string output = task.action is not null
				? await task.action()
				: await runProcessAsync(task.exe!, task.args ?? "");

			foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries))
			{
				appendLine(line.TrimEnd(), "#a0a0a0");
			}

			appendLine("", null);

			double progress = (double)step_number / total * 100.0;
			await Dispatcher.InvokeAsync(() => ProgressBar.Value = progress);

			await Task.Delay(120);
		}

		var engine = new ToastEngine(m_project);
		Close();
	}

	private void appendLine(string text, string? color)
	{
		Dispatcher.Post(() =>
		{
			ConsoleOutput.Text += text + '\n';
			ConsoleScroll.ScrollToEnd();
		});
	}

	private Task<string> runProcessAsync(string exe, string args)
	{
		return Task.Run(() =>
		{
			try
			{
				using var proc = new Process();
				proc.StartInfo = new ProcessStartInfo
				{
					FileName = exe,
					Arguments = args,
					WorkingDirectory = Path.GetDirectoryName(m_project),
					RedirectStandardOutput = true,
					RedirectStandardError = true,
					UseShellExecute = false,
					CreateNoWindow = true
				};

				var sb = new StringBuilder();
				proc.OutputDataReceived += (_, e) =>
				{
					if (e.Data is not null) sb.AppendLine(e.Data);
				};
				proc.ErrorDataReceived += (_, e) =>
				{
					if (e.Data is not null) sb.AppendLine(e.Data);
				};

				proc.Start();
				proc.BeginOutputReadLine();
				proc.BeginErrorReadLine();
				proc.WaitForExit();

				return sb.Length > 0 ? sb.ToString() : "(No output)";
			}
			catch (Exception e)
			{
				return $"error: {e.Message}";
			}
		});
	}
}
