//
// WorkspaceViewModel.cs by Xein
// 12 May 2026
//

using System;
using CommunityToolkit.Mvvm.ComponentModel;
using Dock.Model.Controls;

namespace editor.Workspace;

public partial class WorkspaceViewModel : ViewModelBase {
	private readonly ToastEngine m_toast;
	private readonly DockFactory m_dockFactory;
	private readonly ToastZoneFactory m_toastZoneFactory;
	public IRootDock MainLayout { get; set; }
	public IRootDock ToastZoneLayout { get; set; }

	[ObservableProperty]
	private bool m_toastZoneActive;

	private bool m_toastZonePinned;

	public WorkspaceViewModel(ToastEngine toast) {
		m_toast = toast;

		// Main docking area
		m_dockFactory = new DockFactory(toast);
		MainLayout = m_dockFactory.CreateLayout();
		m_dockFactory.InitLayout(MainLayout);

		// Toast docking area
		m_toastZoneFactory = new ToastZoneFactory();
		ToastZoneLayout = m_toastZoneFactory.CreateLayout();
		m_toastZoneFactory.InitLayout(ToastZoneLayout);

	}

	public void ShowToastZone(bool active) {
		if (m_toastZonePinned == true) return;
		ToastZoneActive = active;
	}

	public void PinToastZone() {
		m_toastZonePinned = !m_toastZonePinned;
		ToastZoneActive = m_toastZonePinned;
	}
}
