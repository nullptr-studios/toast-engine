//
// StartWindowViewModel.cs by Xein
// 26 May 2026
//

using editor.Models;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using editor.Views;
using Tomlyn;
using Tomlyn.Model;
using CommunityToolkit.Mvvm.Input;

namespace editor.ViewModels;

public partial class StartWindowViewModel : ViewModelBase {
	private ProjectList m_project_list = ProjectList.loadList();

	public List<ProjectListItem> projects => m_project_list.projects;
	public void saveProjects() => m_project_list.saveList();

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
			var project_data = TomlSerializer.Deserialize<TomlTable>(project_path);
			m_project_list.projects.Add(new ProjectListItem {
				title = project_data?["name"].ToString() ?? "Untitled Project",
				path = project_path,
				date = System.DateTime.Now.ToString("dd MMM YYYY HH:mm"),
				version = project_data?["version"].ToString() ?? "",
				thumbnail_path = Path.Combine(Path.GetDirectoryName(project_path)!, ".toast", "thumbnails", "project.png")
			});

			// TODO: Open the actual project lol

			parent_window.Close();
		}
	}

	[RelayCommand]
	public async Task newProject(Window parent_window) {
		var dialog = new NewProjectWindow();
		dialog.WindowStartupLocation = WindowStartupLocation.CenterOwner;
		var result = await dialog.ShowDialog<bool>(parent_window);

		if (result) {
			m_project_list.projects.Add(new ProjectListItem {
				title = dialog.project_title,
				path = dialog.project_path,
				date = System.DateTime.Now.ToString("dd MMM yyyy HH:mm"),
				version = dialog.project_version,
				thumbnail_path = dialog.project_thumbnail
			});

			// TODO: Open the actual project

			parent_window.Close();
		}
	}

	[RelayCommand]
	public void removeProject(ProjectListItem item) {
		m_project_list.projects.Remove(item);
	}
}