using Avalonia.Controls;

namespace editor.Components.Modals;

public partial class LoadingPopup : Window {
	public LoadingPopup() : this("Loading") { }

	public LoadingPopup(string text) {
		InitializeComponent();
		Message.Text = text;
	}
}
