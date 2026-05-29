//
// StartWindowViewModel.cs by Xein
// 26 May 2026
//

using editor.Models;
using System.Collections.ObjectModel;
using System.IO;
using System.Threading.Tasks;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using editor.Views;
using Tomlyn;
using Tomlyn.Model;
using CommunityToolkit.Mvvm.Input;

namespace editor.ViewModels;

public partial class StartWindowViewModel : ViewModelBase {
	private ProjectList m_project_list = ProjectList.loadList();
	private Window? m_window;

	public ObservableCollection<ProjectListItem> projects => m_project_list.projects;
	public void saveProjects() => m_project_list.saveList();
	public void setWindow(Window window) => m_window = window;

	private void LaunchProject(string project_path, Window parent_window) {
		var project_item = m_project_list.projects.FirstOrDefault(p => p.path == project_path);
		if (project_item.path != null) {
			project_item.date = System.DateTime.Now.ToString("dd MMM yyyy HH:mm");
		}

		var splash_screen = new SplashWindow(project_path);
		splash_screen.Show();
		parent_window.Close();
	}

	private void LaunchProjectFromList(string project_path, Window parent_window) {
		if (!File.Exists(project_path)) {
			var project_to_remove = m_project_list.projects.FirstOrDefault(p => p.path == project_path);
			if (project_to_remove.path != null) {
				removeProject(project_to_remove);
			}
			return;
		}

		LaunchProject(project_path, parent_window);
	}

	[RelayCommand]
	public async Task openProject(Window parent_window) {
		var top_level = TopLevel.GetTopLevel(parent_window);
		var files = await top_level.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions {
			Title = "Open Project",
			AllowMultiple = false,
			FileTypeFilter = [
				new FilePickerFileType("Toast Project") {
					Patterns = ["*.toast"]
				}
			]
		});

		if (files.Count > 0) {
			var project_path = files[0].Path.LocalPath;
			
			if (!m_project_list.projects.Any(p => p.path == project_path)) {
				try {
					var project_content = File.ReadAllText(project_path);
					var project_data = TomlSerializer.Deserialize<TomlTable>(project_content);
					var thumbnail_path = Path.Combine(Path.GetDirectoryName(project_path)!, ".toast", "thumbnails", "project.png");
					
					m_project_list.projects.Add(new ProjectListItem {
						title = project_data?["name"].ToString() ?? "Untitled Project",
						path = project_path,
						date = System.DateTime.Now.ToString("dd MMM yyyy HH:mm"),
						version = project_data?["version"].ToString() ?? "",
						thumbnail_path = File.Exists(thumbnail_path) ? thumbnail_path : ""
					});
				}
				catch {
					m_project_list.projects.Add(new ProjectListItem {
						title = "Untitled Project",
						path = project_path,
						date = System.DateTime.Now.ToString("dd MMM yyyy HH:mm"),
						version = "",
						thumbnail_path = ""
					});
				}
			}

			LaunchProject(project_path, parent_window);
		}
	}

	[RelayCommand]
	public async Task newProject(Window parent_window) {
		var dialog = new NewProjectWindow();
		dialog.WindowStartupLocation = WindowStartupLocation.CenterOwner;
		var result = await dialog.ShowDialog<bool>(parent_window);

		if (result) {
			if (!m_project_list.projects.Any(p => p.path == dialog.project_path)) {
				try {
					var project_content = File.ReadAllText(dialog.project_path);
					var project_data = TomlSerializer.Deserialize<TomlTable>(project_content);
					var thumbnail_path = dialog.project_thumbnail;
					
					m_project_list.projects.Add(new ProjectListItem {
						title = dialog.project_title,
						path = dialog.project_path,
						date = System.DateTime.Now.ToString("dd MMM yyyy HH:mm"),
						version = project_data?["version"].ToString() ?? dialog.project_version,
						thumbnail_path = File.Exists(thumbnail_path) ? thumbnail_path : ""
					});
				}
				catch {
					m_project_list.projects.Add(new ProjectListItem {
						title = dialog.project_title,
						path = dialog.project_path,
						date = System.DateTime.Now.ToString("dd MMM yyyy HH:mm"),
						version = dialog.project_version,
						thumbnail_path = ""
					});
				}
			}

			LaunchProject(dialog.project_path, parent_window);
		}
	}

	[RelayCommand]
	public void openProjectFromList(ProjectListItem item) {
		if (m_window != null) {
			LaunchProjectFromList(item.path, m_window);
		}
	}

	[RelayCommand]
	public void removeProject(ProjectListItem item) {
		m_project_list.projects.Remove(item);
	}
}