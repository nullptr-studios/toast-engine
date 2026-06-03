//
// StartWindowViewModel.cs by Xein
// 26 May 2026
//

using System.Collections.ObjectModel;
using System.IO;
using System.Threading.Tasks;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using Tomlyn;
using Tomlyn.Model;
using CommunityToolkit.Mvvm.Input;
using editor.Loader;

namespace editor.StartWindow;

public partial class StartWindowViewModel : ViewModelBase {
	private ProjectList m_projectList = ProjectList.LoadList();
	private Window? m_window;

	public ObservableCollection<ProjectListItem> Projects => m_projectList.Projects;
	public void SaveProjects() => m_projectList.SaveList();
	public void SetWindow(Window window) => m_window = window;

	private void LaunchProject(string projectPath, Window parentWindow) {
		var projectItem = m_projectList.Projects.FirstOrDefault(p => p.Path == projectPath);
		if (projectItem.Path != null) {
			projectItem.Date = System.DateTime.Now.ToString("dd MMM yyyy HH:mm");
		}

		var splashScreen = new SplashWindow(projectPath);
		splashScreen.Show();
		parentWindow.Close();
	}

	private void LaunchProjectFromList(string projectPath, Window parentWindow) {
		if (!File.Exists(projectPath)) {
			var projectToRemove = m_projectList.Projects.FirstOrDefault(p => p.Path == projectPath);
			if (projectToRemove.Path != null) {
				RemoveProject(projectToRemove);
			}
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
			
			if (m_projectList.Projects.All(p => p.Path != projectPath)) {
				try {
					var projectContent = File.ReadAllText(projectPath);
					var projectData = TomlSerializer.Deserialize<TomlTable>(projectContent);
					var thumbnailPath = Path.Combine(Path.GetDirectoryName(projectPath)!, ".toast", "thumbnails", "project.png");
					
					m_projectList.Projects.Add(new ProjectListItem {
						Title = projectData?["name"].ToString() ?? "Untitled Project",
						Path = projectPath,
						Date = System.DateTime.Now.ToString("dd MMM yyyy HH:mm"),
						Version = projectData?["version"].ToString() ?? "",
						ThumbnailPath = File.Exists(thumbnailPath) ? thumbnailPath : ""
					});
				}
				catch {
					m_projectList.Projects.Add(new ProjectListItem {
						Title = "Untitled Project",
						Path = projectPath,
						Date = System.DateTime.Now.ToString("dd MMM yyyy HH:mm"),
						Version = "",
						ThumbnailPath = ""
					});
				}
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
			if (m_projectList.Projects.All(p => p.Path != dialog.ProjectPath)) {
				try {
					var projectContent = File.ReadAllText(dialog.ProjectPath);
					var projectData = TomlSerializer.Deserialize<TomlTable>(projectContent);
					var thumbnailPath = dialog.ProjectThumbnail;
					
					m_projectList.Projects.Add(new ProjectListItem {
						Title = dialog.ProjectTitle,
						Path = dialog.ProjectPath,
						Date = System.DateTime.Now.ToString("dd MMM yyyy HH:mm"),
						Version = projectData?["version"].ToString() ?? dialog.ProjectVersion,
						ThumbnailPath = File.Exists(thumbnailPath) ? thumbnailPath : ""
					});
				}
				catch {
					m_projectList.Projects.Add(new ProjectListItem {
						Title = dialog.ProjectTitle,
						Path = dialog.ProjectPath,
						Date = System.DateTime.Now.ToString("dd MMM yyyy HH:mm"),
						Version = dialog.ProjectVersion,
						ThumbnailPath = ""
					});
				}
			}

			LaunchProject(dialog.ProjectPath, parentWindow);
		}
	}

	[RelayCommand]
	public void OpenProjectFromList(ProjectListItem item) {
		if (m_window != null) {
			LaunchProjectFromList(item.Path, m_window);
		}
	}

	[RelayCommand]
	public void RemoveProject(ProjectListItem item) {
		m_projectList.Projects.Remove(item);
	}
}