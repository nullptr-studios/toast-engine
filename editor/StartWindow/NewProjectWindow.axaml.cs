using System;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using editor.Components.Modals;
using Lucide.Avalonia;
using Tomlyn;
using Tomlyn.Model;

namespace editor.StartWindow;

public partial class NewProjectWindow : Window {
	private string m_baseFolderPath;
	private string? m_projectFolder;

	public NewProjectWindow() {
		InitializeComponent();

		m_baseFolderPath = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
		UpdateProjectData();
	}

	public string ProjectTitle { get; private set; } = "";
	public string ProjectPath { get; private set; } = "";
	public string ProjectVersion { get; private set; } = "v1.0.0";
	public string ProjectThumbnail { get; private set; } = "";

	private void UpdateProjectData() {
		ProjectTitle = string.IsNullOrWhiteSpace(TitleTextbox.Text) ? "Untitled Project" : TitleTextbox.Text;
		var formattedTitle = ProjectTitle.ToLowerInvariant().Replace(" ", "_");

		var projectDirectory = Path.Combine(m_baseFolderPath, formattedTitle);
		m_projectFolder = projectDirectory;

		PathTextbox?.Text = projectDirectory;

		ProjectPath = Path.Combine(projectDirectory, formattedTitle + ".toast");
		ProjectThumbnail = Path.Combine(projectDirectory, ".toast", "thumbnails", "project.png");
	}

	private void Name_OnTextChanged(object? sender, TextChangedEventArgs e) {
		UpdateProjectData();
	}

	private async void Folder_OnClick(object? sender, RoutedEventArgs e) {
		var topLevel = GetTopLevel(this);
		if (topLevel == null) return;

		var folders = await topLevel.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions {
			Title = "Select Project Location",
			AllowMultiple = false
		});

		if (folders.Count <= 0) return;

		m_baseFolderPath = folders[0].Path.LocalPath;
		UpdateProjectData();
	}

	private async void Create_OnClick(object? sender, RoutedEventArgs e) {
		UpdateProjectData();
		ProjectVersion = "v1.0.0";

		try {
			if (Directory.Exists(m_projectFolder))
				throw new IOException($"A folder already exists at \"{m_projectFolder}\".");

			Directory.CreateDirectory(m_projectFolder!);

			// TODO: This should be created in c++
			var projectFile = new TomlTable {
				["name"] = ProjectTitle,
				["version"] = ProjectVersion
			};

			var projectFileStr = TomlSerializer.Serialize(projectFile);
			File.WriteAllText(ProjectPath, projectFileStr);

			Directory.CreateDirectory(Path.Combine(m_projectFolder ?? throw new InvalidOperationException(), ".toast"));
			Directory.CreateDirectory(Path.Combine(m_projectFolder, "artwork"));
			Directory.CreateDirectory(Path.Combine(m_projectFolder, "assets"));
			Directory.CreateDirectory(Path.Combine(m_projectFolder, "lib"));
			Directory.CreateDirectory(Path.Combine(m_projectFolder, "build"));

			// Gitignore
			File.Copy("Resources/files/project.gitignore", Path.Combine(m_projectFolder, ".gitignore"));

			// C++ library
			Directory.CreateDirectory(Path.Combine(m_projectFolder, "lib", "src"));
			Directory.CreateDirectory(Path.Combine(m_projectFolder, "lib", "src", "_detail"));
			File.Copy("Resources/files/lib/CMakeLists.txt", Path.Combine(m_projectFolder, "lib", "CMakeLists.txt"));
			File.Copy("Resources/files/lib/src/my_game.hpp", Path.Combine(m_projectFolder, "lib", "src", "my_game.hpp"));
			File.Copy("Resources/files/lib/src/_detail/game.h",
				Path.Combine(m_projectFolder, "lib", "src", "_detail", "game.h"));
			File.Copy("Resources/files/lib/src/_detail/game.cpp",
				Path.Combine(m_projectFolder, "lib", "src", "_detail", "game.cpp"));

			Close(true);
		}
		catch (Exception ex) {
			var modal = new MessageModal(new ModalConfig(
				"Failed to create project",
				ex.Message,
				Icon: LucideIconKind.CircleX,
				IconColor: Application.Current!.TryGetResource("Red", null, out var r) ? r as SolidColorBrush : null
			));
			await modal.ShowDialog(this);
			Close(false);
		}
	}

	private void Exit_OnClick(object? sender, RoutedEventArgs e) {
		Close(false);
	}
}
