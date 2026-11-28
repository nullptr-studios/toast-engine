using Avalonia.Controls;
using editor.Settings;

namespace editor.Workspace;

public sealed class ProjectSettingsWindow : Window {
	public ProjectSettingsWindow(ProjectSettingsViewModel viewModel) {
		var state = SettingsPanelState.Current;
		Title = "Project Settings";
		Width = state.WindowWidth;
		Height = state.WindowHeight;
		MinWidth = 640;
		MinHeight = 460;
		WindowStartupLocation = WindowStartupLocation.CenterOwner;
		DataContext = viewModel;
		Content = new ProjectSettingsView { DataContext = viewModel };

		Closing += (_, _) => {
			if (WindowState != WindowState.Normal) return;
			state.WindowWidth = Width;
			state.WindowHeight = Height;
			state.Save();
		};
	}
}
