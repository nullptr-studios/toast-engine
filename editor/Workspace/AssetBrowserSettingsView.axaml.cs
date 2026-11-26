//
// AssetBrowserSettingsView.cs by Xein
// 24 Nov 2026
//

using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace editor.Workspace;

public partial class AssetBrowserSettingsView : UserControl {
	public AssetBrowserSettingsView() {
		InitializeComponent();
	}

	private void InitializeComponent() {
		AvaloniaXamlLoader.Load(this);
	}
}
