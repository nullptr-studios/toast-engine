using System;
using System.IO;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Tomlyn.Model;
using Tomlyn;

namespace editor.StartWindow;

public partial class NewProjectWindow : Window {
	public string ProjectTitle { get; private set; } = "";
	public string ProjectPath { get; private set; } = "";
	public string ProjectVersion { get; private set; } = "v1.0.0";
	public string ProjectThumbnail { get; private set; } = "";

	private string m_baseFolderPath;
	private string m_projectFolder;

	public NewProjectWindow() {
		InitializeComponent();

		m_baseFolderPath = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
		UpdateProjectData();
	}

	private void UpdateProjectData() {
		ProjectTitle = string.IsNullOrWhiteSpace(TitleTextbox.Text) ? "Untitled Project" : TitleTextbox.Text;
		var formattedTitle = ProjectTitle.ToLowerInvariant().Replace(" ", "_");

		var projectDirectory = Path.Combine(m_baseFolderPath, formattedTitle);
		m_projectFolder = projectDirectory;

		if (PathTextbox != null) {
			PathTextbox.Text = projectDirectory;
		}

		ProjectPath = Path.Combine(projectDirectory, formattedTitle + ".toast");
		ProjectThumbnail = Path.Combine(projectDirectory, ".toast", "thumbnails", "project.png");
	}

	private void Name_OnTextChanged(object? sender, TextChangedEventArgs e) {
		UpdateProjectData();
	}

	private async void Folder_OnClick(object? sender, RoutedEventArgs e) {
		var topLevel = TopLevel.GetTopLevel(this);
		if (topLevel == null) return;

		var folders = await topLevel.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions() {
			Title = "Select Project Location",
			AllowMultiple = false,
		});

		if (folders.Count <= 0) return;

		m_baseFolderPath = folders[0].Path.LocalPath;
		UpdateProjectData();
	}

	private void Create_OnClick(object? sender, RoutedEventArgs e) {
		UpdateProjectData();
		ProjectVersion = "v1.0.0";

		if (Directory.Exists(m_projectFolder)) {
			// TODO: Error
			return;
		}

		Directory.CreateDirectory(m_projectFolder);

		// TODO: This should be created in c++
		var projectFile = new TomlTable {
			["name"] = ProjectTitle,
			["version"] = ProjectVersion
		};

		var projectFileStr = TomlSerializer.Serialize(projectFile);
		File.WriteAllText(ProjectPath,  projectFileStr);

		Directory.CreateDirectory(Path.Combine(m_projectFolder, ".toast"));
		Directory.CreateDirectory(Path.Combine(m_projectFolder, "artwork"));
		Directory.CreateDirectory(Path.Combine(m_projectFolder, "assets"));
		Directory.CreateDirectory(Path.Combine(m_projectFolder, "lib"));
		Directory.CreateDirectory(Path.Combine(m_projectFolder, "build"));

		// Gitignore
		File.Copy("Resources/files/project.gitignore", Path.Combine(m_projectFolder, ".gitignore"));

		// C++ library
		Directory.CreateDirectory(Path.Combine(m_projectFolder, "lib", "src"));
		Directory.CreateDirectory(Path.Combine(m_projectFolder, "lib", "src", "_detail"));
		File.Copy("Resources/files/lib/CMakeLists.txt",  Path.Combine(m_projectFolder, "lib", "CMakeLists.txt"));
		File.Copy("Resources/files/lib/src/my_game.hpp",  Path.Combine(m_projectFolder, "lib", "src", "my_game.hpp"));
		File.Copy("Resources/files/lib/src/_detail/game.h",  Path.Combine(m_projectFolder, "lib", "src", "_detail","game.h"));
		File.Copy("Resources/files/lib/src/_detail/game.cpp",  Path.Combine(m_projectFolder, "lib", "src", "_detail","game.cpp"));

		Close(true);
	}

	private void Exit_OnClick(object? sender, RoutedEventArgs e) {
		Close(false);
	}
}
