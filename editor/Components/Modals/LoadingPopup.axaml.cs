using Avalonia.Controls;
using Avalonia.Media;
using editor.Components.Elements;
using editor.Git;

namespace editor.Components.Modals;

public partial class LoadingPopup : Window {
	public LoadingPopup() : this("Loading") { }

	public LoadingPopup(string text) {
		InitializeComponent();
		Message.Text = text;
	}

	public void SetLock(OpenLockState state) {
		LockRow.Children.Clear();
		LockRow.IsVisible = state.Kind != OpenLockKind.None;
		if (!LockRow.IsVisible) return;

		var mine = state.Kind == OpenLockKind.Mine;
		LockRow.Children.Add(new GitChip { Kind = mine ? GitChipKind.LockMine : GitChipKind.LockOther });
		LockRow.Children.Add(new TextBlock {
			Text = mine ? "Locked by you" : $"Locked by {state.Owner}",
			Foreground = mine ? GitStatusStyle.Green : GitStatusStyle.Orange,
			VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center
		});
	}
}
