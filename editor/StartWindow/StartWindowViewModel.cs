//
// StartWindowViewModel.cs by Xein
// 26 May 2026
//

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.Input;
using editor.Assets;
using editor.Components.Modals;
using editor.Engine;
using Tomlyn;
using Tomlyn.Model;

namespace editor.StartWindow;

public partial class StartWindowViewModel : ViewModelBase {
	private static readonly string ToastPath =
		Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "../toast_engine"));

	private readonly ProjectList m_projectList = ProjectList.LoadList();
	private Window? m_window;

	public ObservableCollection<ProjectListItem> Projects => m_projectList.Projects;

	public void SaveProjects() {
		m_projectList.SaveList();
	}

	public void SetWindow(Window window) {
		m_window = window;
	}

	private void LaunchProject(string projectPath, Window parentWindow) {
		var projectItem = m_projectList.Projects.FirstOrDefault(p => p.Path == projectPath);
		if (projectItem.Path != null) projectItem.Date = DateTime.Now.ToString("dd MMM yyyy HH:mm");

		var projectDir = Directory.Exists(projectPath) ? projectPath : Path.GetDirectoryName(projectPath)!;
		var corePath = Path.Combine(ToastPath, "bin", "assets");

		if (!ProjectContext.IsInitialized)
			ProjectContext.Initialize(projectDir, corePath);

		var hasGit = Directory.Exists(Path.Combine(projectDir, ".git"));
		var tasks = new List<LoaderTask>();

		if (!hasGit) {
			tasks.Add(LoaderTask.Run("git init", "git", "init"));
			tasks.Add(LoaderTask.Run("git add .", "git", "add ."));
			tasks.Add(LoaderTask.Run("git commit", "git",
				"commit -m \"Initial commit\" --author \"nullptr Studios <toast-engine@nullptr.es>\""));
		}

		// Generate the game's reflection metadata before configuring
		var libSrc = Path.Combine(projectDir, "lib", "src");
		var libGenerated = Path.Combine(projectDir, "lib", "generated");
		Directory.CreateDirectory(libGenerated);

		var exeExt = OperatingSystem.IsWindows() ? ".exe" : "";
		var refgen = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,
			"..", "reflection_generator", $"reflection_generator{exeExt}"));
		var gameDb = ProjectContext.Resolve("cache://game_reflect.json");
		tasks.Add(LoaderTask.Run("Generate game reflection", refgen,
			$"--database \"{gameDb}\" --output \"{libGenerated}\" --input \"{libSrc}\" " +
			$"--include-root \"{libSrc}\" --register-fn registerGameTypes --attribute Game"));

		// Copy the engine reflection database to cache://
		tasks.Add(LoaderTask.Do("Copy engine reflection", async log => {
			var src = Path.Combine(corePath, "engine_reflect.json");
			var dst = ProjectContext.Resolve("cache://engine_reflect.json");
			if (File.Exists(src)) {
				File.Copy(src, dst, true);
				log($"Copied engine_reflect.json -> {dst}");
			} else {
				log($"warning: engine_reflect.json not found at {src}");
			}

			await Task.CompletedTask;
		}));

		var cmakeGenerator = OperatingSystem.IsWindows() ? "-G \"Visual Studio 18 2026\"" : "-G \"Ninja\"";
		tasks.Add(LoaderTask.Run("cmake lib/ -B .toast/cmake_cache", "cmake",
			$"lib/ -B .toast/cmake_cache {cmakeGenerator} -DTOAST_PATH={ToastPath}"));
		tasks.Add(LoaderTask.Run("cmake --build .toast/cmake_cache", "cmake",
			"--build .toast/cmake_cache"));

		var vm = new LoaderViewModel(tasks) {
			OnComplete = async () => {
				await Dispatcher.UIThread.InvokeAsync(() => { }, DispatcherPriority.Render);
				_ = new ToastEngine(projectPath);
			}
		};

		var desktop = (IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!;
		desktop.MainWindow = new SplashLoaderWindow(vm);
		desktop.MainWindow.Show();
		parentWindow.Close();
	}

	private void LaunchProjectFromList(string projectPath, Window parentWindow) {
		if (!File.Exists(projectPath)) {
			var projectToRemove = m_projectList.Projects.FirstOrDefault(p => p.Path == projectPath);
			if (projectToRemove.Path != null) RemoveProject(projectToRemove);
			return;
		}

		LaunchProject(projectPath, parentWindow);
	}

	[RelayCommand]
	public async Task OpenProject(Window parentWindow) {
		var topLevel = TopLevel.GetTopLevel(parentWindow);
		var files = await topLevel!.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions {
			Title = "Open Project",
			AllowMultiple = false,
			FileTypeFilter = [
				new FilePickerFileType("Toast Project") {
					Patterns = ["*.toast"]
				}
			]
		});

		if (files.Count > 0) {
			var projectPath = files[0].Path.LocalPath;

			if (m_projectList.Projects.All(p => p.Path != projectPath))
				try {
					var projectContent = File.ReadAllText(projectPath);
					var projectData = TomlSerializer.Deserialize<TomlTable>(projectContent);
					var thumbnailPath = Path.Combine(Path.GetDirectoryName(projectPath)!, ".toast", "thumbnails",
						"project.png");

					m_projectList.Projects.Add(new ProjectListItem {
						Title = projectData?["name"].ToString() ?? "Untitled Project",
						Path = projectPath,
						Date = DateTime.Now.ToString("dd MMM yyyy HH:mm"),
						Version = projectData?["version"].ToString() ?? "",
						ThumbnailPath = File.Exists(thumbnailPath) ? thumbnailPath : ""
					});
				} catch {
					m_projectList.Projects.Add(new ProjectListItem {
						Title = "Untitled Project",
						Path = projectPath,
						Date = DateTime.Now.ToString("dd MMM yyyy HH:mm"),
						Version = "",
						ThumbnailPath = ""
					});
				}

			LaunchProject(projectPath, parentWindow);
		}
	}

	[RelayCommand]
	public async Task NewProject(Window parentWindow) {
		var dialog = new NewProjectWindow {
			WindowStartupLocation = WindowStartupLocation.CenterOwner
		};
		var result = await dialog.ShowDialog<bool>(parentWindow);

		if (result) {
			if (m_projectList.Projects.All(p => p.Path != dialog.ProjectPath))
				try {
					var projectContent = File.ReadAllText(dialog.ProjectPath);
					var projectData = TomlSerializer.Deserialize<TomlTable>(projectContent);
					var thumbnailPath = dialog.ProjectThumbnail;

					m_projectList.Projects.Add(new ProjectListItem {
						Title = dialog.ProjectTitle,
						Path = dialog.ProjectPath,
						Date = DateTime.Now.ToString("dd MMM yyyy HH:mm"),
						Version = projectData?["version"].ToString() ?? dialog.ProjectVersion,
						ThumbnailPath = File.Exists(thumbnailPath) ? thumbnailPath : ""
					});
				} catch {
					m_projectList.Projects.Add(new ProjectListItem {
						Title = dialog.ProjectTitle,
						Path = dialog.ProjectPath,
						Date = DateTime.Now.ToString("dd MMM yyyy HH:mm"),
						Version = dialog.ProjectVersion,
						ThumbnailPath = ""
					});
				}

			LaunchProject(dialog.ProjectPath, parentWindow);
		}
	}

	[RelayCommand]
	public void OpenProjectFromList(ProjectListItem item) {
		if (m_window != null) LaunchProjectFromList(item.Path, m_window);
	}

	[RelayCommand]
	public void RemoveProject(ProjectListItem item) {
		m_projectList.Projects.Remove(item);
	}
}
