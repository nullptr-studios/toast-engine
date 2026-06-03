//
// EditorDockFactory.cs by Xein
// 4 Jun 2026
//

using System;
using System.Collections.Generic;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;

namespace editor.MainWindow;

public class EditorDockFactory(ToastEngine toast) : Factory {
	public override IRootDock CreateLayout() {
		var defaultWorkspace = new WorkspaceViewModel("Scene 1", toast);
		var secondWorkspace = new WorkspaceViewModel("Scene 2", toast);

		var documentDock = new DocumentDock {
			Id = "Workspaces",
			Title = "Workspaces",
			IsCollapsable = false,
			Proportion = double.NaN,
			ActiveDockable = defaultWorkspace,
			VisibleDockables = CreateList<IDockable>(defaultWorkspace, secondWorkspace),
			CanFloat = true, // Enables tearing off the tab into a new window
			AllowedDockOperations = DockOperationMask.Fill | DockOperationMask.Window,
			AllowedDropOperations = DockOperationMask.Fill
		};

		var rootDock = CreateRootDock();
		rootDock.IsCollapsable = false;
		rootDock.ActiveDockable = documentDock;
		rootDock.DefaultDockable = documentDock;
		rootDock.VisibleDockables = CreateList<IDockable>(documentDock);

		return rootDock;
	}

	public override void InitLayout(IDockable layout) {
		ContextLocator = new Dictionary<string, Func<object?>> {
			["Workspaces"] = () => layout
		};

		HostWindowLocator = new Dictionary<string, Func<IHostWindow?>> {
			[nameof(IDockWindow)] = () => new ToastHostWindow(toast)
		};
		DefaultHostWindowLocator = () => new ToastHostWindow(toast);

		base.InitLayout(layout);
	}
}
