//
// ProjectSettingsView.axaml.cs
// 22 Sep 2026
//

using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace editor.Workspace;

public partial class ProjectSettingsView : UserControl {
	public ProjectSettingsView() {
		InitializeComponent();

		AttachedToVisualTree += (_, _) => (DataContext as ProjectSettingsViewModel)?.StartPolling();
		DetachedFromVisualTree += (_, _) => (DataContext as ProjectSettingsViewModel)?.StopPolling();
	}

	private void InitializeComponent() {
		AvaloniaXamlLoader.Load(this);
	}
}
