using Avalonia.Controls;

namespace editor.Git;

public sealed class LockArtworkWindow : Window {
	public LockArtworkWindow(LockArtworkViewModel viewModel) {
		Title = "Lock Artwork";
		Width = 760;
		Height = 600;
		MinWidth = 520;
		MinHeight = 360;
		WindowStartupLocation = WindowStartupLocation.CenterOwner;
		DataContext = viewModel;
		Content = new LockArtworkView { DataContext = viewModel };
		viewModel.Done += Close;
	}
}
