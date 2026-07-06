using Avalonia.Controls;

namespace editor.Workspace;

// viewport rendering and input forwarding live in ViewportControl; toolbar state in WorkspaceViewModel
public partial class WorkspaceView : UserControl {
	public WorkspaceView() {
		InitializeComponent();
	}
}
