using System;
using System.Collections.Generic;
using Dock.Avalonia.Controls;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;

namespace editor.MainWindow;

public class EditorDockFactory : Factory
{
	private readonly ToastEngine m_toast;

	public EditorDockFactory(ToastEngine toast) {
		m_toast = toast;
	}

	public override IRootDock CreateLayout()
	{
		// Create initial workspaces
		var default_workspace = new WorkspaceViewModel("Scene 1");
		var second_workspace = new WorkspaceViewModel("Scene 2");

		// The DocumentDock is what allows tabs and detaching
		var document_dock = new DocumentDock
		{
			Id = "Workspaces",
			Title = "Workspaces",
			IsCollapsable = false,
			Proportion = double.NaN,
			ActiveDockable = default_workspace,
			VisibleDockables = CreateList<IDockable>(default_workspace, second_workspace),
			CanFloat = true, // Enables tearing off the tab into a new window
			AllowedDockOperations = DockOperationMask.Fill | DockOperationMask.Window,
			AllowedDropOperations = DockOperationMask.Fill
		};

		var root_dock = CreateRootDock();
		root_dock.IsCollapsable = false;
		root_dock.ActiveDockable = document_dock;
		root_dock.DefaultDockable = document_dock;
		root_dock.VisibleDockables = CreateList<IDockable>(document_dock);

		return root_dock;
	}

	public override void InitLayout(IDockable layout)
	{
		ContextLocator = new Dictionary<string, Func<object?>> {
			["Workspaces"] = () => layout
		};

		HostWindowLocator = new Dictionary<string, Func<IHostWindow?>> {
			[nameof(IDockWindow)] = () => new ToastHostWindow(m_toast)
		};
		DefaultHostWindowLocator = () => new ToastHostWindow(m_toast);

		base.InitLayout(layout);
	}
}
