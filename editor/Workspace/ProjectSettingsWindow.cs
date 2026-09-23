using Avalonia.Controls;

namespace editor.Workspace;

public sealed class ProjectSettingsWindow : Window {
	public ProjectSettingsWindow(ProjectSettingsViewModel viewModel) {
		Title = "Project Settings";
		Width = 1200;
		Height = 800;
		MinWidth = 800;
		MinHeight = 560;
		WindowStartupLocation = WindowStartupLocation.CenterOwner;
		DataContext = viewModel;
		Content = new ProjectSettingsView { DataContext = viewModel };
	}
}
