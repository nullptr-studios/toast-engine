using Avalonia.Controls;

namespace editor.Git;

public sealed class CommitWindow : Window {
	public CommitWindow(CommitViewModel viewModel) {
		Title = "Commit";
		Width = 1000;
		Height = 640;
		MinWidth = 720;
		MinHeight = 440;
		WindowStartupLocation = WindowStartupLocation.CenterOwner;
		DataContext = viewModel;
		Content = new CommitView { DataContext = viewModel };
		Closed += (_, _) => viewModel.Dispose();
	}
}
