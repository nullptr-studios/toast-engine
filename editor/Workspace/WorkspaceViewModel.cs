//
// WorkspaceViewModel.cs by Xein
// 12 May 2026
//

namespace editor.Workspace;

public class WorkspaceViewModel : ViewModelBase {
	public ViewportViewModel Viewport { get; }

	public WorkspaceViewModel(ToastEngine toast) {
		Viewport = new ViewportViewModel("Scene", toast);
	}
}
