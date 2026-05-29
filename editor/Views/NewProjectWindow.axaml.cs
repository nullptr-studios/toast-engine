using System;
using System.IO;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Tomlyn.Model;
using Tomlyn;

namespace editor.Views;

public partial class NewProjectWindow : Window {
	public string project_title { get; private set; } = "";
	public string project_path { get; private set; } = "";
	public string project_version { get; private set; } = "v1.0.0";
	public string project_thumbnail { get; private set; } = "";

	private string m_base_folder_path;
	private string m_project_folder;

	public NewProjectWindow() {
		InitializeComponent();

		m_base_folder_path = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
		updateProjectData();
	}

	private void updateProjectData() {
		project_title = string.IsNullOrWhiteSpace(TitleTextbox.Text) ? "Untitled Project" : TitleTextbox.Text;
		var formatted_title = project_title.ToLowerInvariant().Replace(" ", "_");

		var project_directory = Path.Combine(m_base_folder_path, formatted_title);
		m_project_folder = project_directory;

		if (PathTextbox != null) {
			PathTextbox.Text = project_directory;
		}

		project_path = Path.Combine(project_directory, formatted_title + ".toast");
		project_thumbnail = Path.Combine(project_directory, ".toast", "thumbnails", "project.png");
	}

	private void Name_OnTextChanged(object? sender, TextChangedEventArgs e) {
		updateProjectData();
	}

	private async void Folder_OnClick(object? sender, RoutedEventArgs e) {
		var top_level = TopLevel.GetTopLevel(this);
		if (top_level == null) return;

		var folders = await top_level.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions() {
			Title = "Select Project Location",
			AllowMultiple = false,
		});

		if (folders.Count <= 0) return;

		m_base_folder_path = folders[0].Path.LocalPath;
		updateProjectData();
	}

	private void Create_OnClick(object? sender, RoutedEventArgs e) {
		updateProjectData();
		project_version = "v1.0.0";

		if (Directory.Exists(m_project_folder)) {
			// TODO: Error
			return;
		}

		Directory.CreateDirectory(m_project_folder);

		// TODO: This should be created in c++
		var project_file = new TomlTable {
			["name"] = project_title,
			["version"] = project_version
		};

		var project_file_str = TomlSerializer.Serialize(project_file);
		File.WriteAllText(project_path,  project_file_str);

		Directory.CreateDirectory(Path.Combine(m_project_folder, ".toast"));
		Directory.CreateDirectory(Path.Combine(m_project_folder, "artwork"));
		Directory.CreateDirectory(Path.Combine(m_project_folder, "assets"));
		Directory.CreateDirectory(Path.Combine(m_project_folder, "lib"));
		Directory.CreateDirectory(Path.Combine(m_project_folder, "build"));

		// Gitignore
		File.Copy("res/files/project.gitignore", Path.Combine(m_project_folder, ".gitignore"));

		// C++ library
		Directory.CreateDirectory(Path.Combine(m_project_folder, "lib", "src"));
		Directory.CreateDirectory(Path.Combine(m_project_folder, "lib", "src", "_detail"));
		File.Copy("res/files/lib/CMakeLists.txt",  Path.Combine(m_project_folder, "lib", "CMakeLists.txt"));
		File.Copy("res/files/lib/src/my_game.hpp",  Path.Combine(m_project_folder, "lib", "src", "my_game.hpp"));
		File.Copy("res/files/lib/src/_detail/game.h",  Path.Combine(m_project_folder, "lib", "src", "_detail","game.h"));
		File.Copy("res/files/lib/src/_detail/game.cpp",  Path.Combine(m_project_folder, "lib", "src", "_detail","game.cpp"));

		Close(true);
	}

	private void Exit_OnClick(object? sender, RoutedEventArgs e) {
		Close(false);
	}
}
