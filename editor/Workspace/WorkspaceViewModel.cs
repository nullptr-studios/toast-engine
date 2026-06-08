//
// WorkspaceViewModel.cs by Xein
// 12 May 2026
//

using Dock.Model.Controls;

namespace editor.Workspace;

public class WorkspaceViewModel : ViewModelBase {
	private readonly ToastEngine m_toast;
	private readonly DockFactory m_dockFactory;
	public IRootDock MainLayout { get; set; }
	public WorkspaceViewModel(ToastEngine toast) {
		m_toast = toast;
		m_dockFactory = new DockFactory(toast);
		MainLayout = m_dockFactory.CreateLayout();
		m_dockFactory.InitLayout(MainLayout);
	}

}
