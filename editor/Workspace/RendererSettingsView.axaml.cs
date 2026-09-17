//
// RendererSettingsView.axaml.cs
// 16 Aug 2026
//

using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace editor.Workspace;

public partial class RendererSettingsView : UserControl {
	public RendererSettingsView() {
		InitializeComponent();

		// The rows cannot be built when the view model is constructed: the dock creates its tools as the shell
		// starts, before a workspace exists and therefore before the renderer has declared anything. Attaching
		// is the first moment the panel is actually being looked at, which is late enough
		AttachedToVisualTree += (_, _) => (DataContext as RendererSettingsViewModel)?.StartPolling();
		DetachedFromVisualTree += (_, _) => (DataContext as RendererSettingsViewModel)?.StopPolling();
	}

	private void InitializeComponent() {
		AvaloniaXamlLoader.Load(this);
	}
}
