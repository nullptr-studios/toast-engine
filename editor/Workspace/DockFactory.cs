using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Dock.Avalonia.Controls;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;

namespace editor.Workspace;

public class DockFactory : Factory {
	private IDocumentDock? m_documentDock;
	private IRootDock? m_rootDock;

	public HierarchyViewModel? Hierarchy { get; private set; }

	// currently focused workspace document, null if none
	public WorkspaceViewModel? ActiveWorkspace => m_documentDock?.ActiveDockable as WorkspaceViewModel;

	public override IRootDock CreateLayout() {
		var hierarchy = new HierarchyViewModel { Id = "Hierarchy", Title = "Hierarchy" };
		Hierarchy = hierarchy;
		var inspector = new InspectorViewModel { Id = "Inspector", Title = "Inspector" };

		var documentDock = new DocumentDock {
			IsCollapsable = false,
			AllowedDropOperations = DockOperationMask.Left | DockOperationMask.Right,
			VisibleDockables = CreateList<IDockable>()
		};

		// left panel (hierarchy)
		var leftDock = new ProportionalDock {
			Proportion = 0.2,
			AllowedDropOperations = DockOperationMask.None,
			Orientation = Orientation.Vertical,
			VisibleDockables = CreateList<IDockable>(
				new ToolDock {
					ActiveDockable = hierarchy,
					AllowedDropOperations = DockOperationMask.Fill | DockOperationMask.Top | DockOperationMask.Bottom,
					VisibleDockables = CreateList<IDockable>(hierarchy),
					Alignment = Alignment.Left,
					GripMode = GripMode.Visible
				}
			)
		};

		// right panel (inspector)
		var rightDock = new ProportionalDock {
			Proportion = 0.2,
			Orientation = Orientation.Vertical,
			VisibleDockables = CreateList<IDockable>(
				new ToolDock {
					ActiveDockable = inspector,
					VisibleDockables = CreateList<IDockable>(inspector),
					Alignment = Alignment.Right,
					GripMode = GripMode.Visible
				}
			)
		};

		var mainLayout = new ProportionalDock {
			Orientation = Orientation.Horizontal,
			IsCollapsable = false,
			VisibleDockables = CreateList<IDockable>(
				leftDock,
				new ProportionalDockSplitter(),
				documentDock,
				new ProportionalDockSplitter(),
				rightDock
			)
		};

		var windowLayout = CreateRootDock();
		windowLayout.Title = "Default";
		windowLayout.IsCollapsable = false;
		windowLayout.VisibleDockables = CreateList<IDockable>(mainLayout);
		windowLayout.ActiveDockable = mainLayout;

		var root = CreateRootDock();
		root.IsCollapsable = false;
		root.VisibleDockables = CreateList<IDockable>(windowLayout);
		root.ActiveDockable = windowLayout;
		root.DefaultDockable = windowLayout;

		m_rootDock = root;
		m_documentDock = documentDock;
		return root;
	}

	public override void InitLayout(IDockable layout) {
		ContextLocator = new Dictionary<string, Func<object?>> {
			["Workspace"] = () => layout,
			["Hierarchy"] = () => layout,
			["Inspector"] = () => layout
		};
		DockableLocator = new Dictionary<string, Func<IDockable?>> {
			["Root"] = () => m_rootDock,
			["Documents"] = () => m_documentDock
		};
		HostWindowLocator = new Dictionary<string, Func<IHostWindow?>> {
			[nameof(IDockWindow)] = () => new HostWindow()
		};
		HideToolsOnClose = true;
		base.InitLayout(layout);
	}

	public override void CloseDockable(IDockable dockable) {
		if (dockable is null) return;

		// intercept workspace close -> show save dialog before actually closing
		if (dockable is WorkspaceViewModel ws && !ws.PendingClose) {
			_ = GatedClose(ws);
			return;
		}

		base.CloseDockable(dockable);
	}

	private async Task GatedClose(WorkspaceViewModel ws) {
		if (await ws.ConfirmCloseAsync()) {
			ws.PendingClose = true;
			CloseDockable(ws);
		}
	}

	public override IDock CreateSplitLayout(IDock dock, IDockable dockable, DockOperation operation) {
		var layout = base.CreateSplitLayout(dock, dockable, operation);
		var isTool = dockable is ITool or IToolDock;
		if (!isTool || layout.VisibleDockables == null) return layout;
		// tools default to 20% width when splitting horizontally, 50% otherwise
		var proportion = operation is DockOperation.Left or DockOperation.Right ? 0.2 : 0.5;
		foreach (var child in layout.VisibleDockables) {
			if (child is not IDock childDock || childDock == dock) continue;
			childDock.Proportion = proportion;
			return layout;
		}

		return layout;
	}

	public WorkspaceViewModel AddWorkspace(WorkspaceViewModel workspace) {
		AddDockable(m_documentDock!, workspace);
		SetActiveDockable(workspace);
		return workspace;
	}
}
