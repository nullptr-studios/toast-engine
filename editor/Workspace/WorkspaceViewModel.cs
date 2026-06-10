//
// WorkspaceViewModel.cs by Xein
// 12 May 2026
//

using System;
using Dock.Model.Controls;
using Dock.Model.Core;

namespace editor.Workspace;

public class WorkspaceViewModel : ViewModelBase {
	private ToastEngine m_toast;

	public WorkspaceViewModel(ToastEngine toast, IRootDock? layout = null) {
		m_toast = toast;
		Factory = layout?.Factory ?? new EditorDockFactory(toast);

		if (layout is null) {
			Layout = Factory.CreateLayout();
			Factory.InitLayout(Layout);
		} else {
			Layout = layout;
			if (Layout.Factory is null) {
				Layout.Factory = Factory;
				Factory.InitLayout(Layout);
			}
		}
	}

	public IFactory Factory { get; }
	public IRootDock Layout { get; }

	public void CloseLayout() {
		if (Layout is IDisposable disposable) disposable.Dispose();
	}
}
