using System;
using System.Collections.Generic;
using Dock.Avalonia.Controls;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;

namespace editor.Workspace;


public class DockFactory(ToastEngine toast) : Factory {
	private IRootDock? m_rootDock;
	private IDocumentDock? m_documentDock;

	public override IRootDock CreateLayout() {
		var document  = new ViewportViewModel(toast) { Id = "Viewport",  Title = "Unnamed Node" };
		var hierarchy = new HierarchyViewModel       { Id = "Hierarchy", Title = "Hierarchy" };
		var inspector = new InspectorViewModel       { Id = "Inspector", Title = "Inspector" };

		var documentDock = new DocumentDock {
			IsCollapsable    = false,
			AllowedDropOperations = DockOperationMask.Left | DockOperationMask.Right,
			ActiveDockable   = document,
			VisibleDockables = CreateList<IDockable>(document),
		};

		var leftDock = new ProportionalDock {
			Proportion       = 0.2,
			AllowedDropOperations = DockOperationMask.None,
			Orientation      = Orientation.Vertical,
			VisibleDockables = CreateList<IDockable>(
				new ToolDock {
					ActiveDockable   = hierarchy,
					AllowedDropOperations = DockOperationMask.Fill | DockOperationMask.Top | DockOperationMask.Bottom,
					VisibleDockables = CreateList<IDockable>(hierarchy),
					Alignment        = Alignment.Left,
					GripMode         = GripMode.Visible,
				}
			),
		};

		var rightDock = new ProportionalDock {
			Proportion       = 0.2,
			Orientation      = Orientation.Vertical,
			VisibleDockables = CreateList<IDockable>(
				new ToolDock {
					ActiveDockable   = inspector,
					VisibleDockables = CreateList<IDockable>(inspector),
					Alignment        = Alignment.Right,
					GripMode         = GripMode.Visible,
				}
			),
		};

		var mainLayout = new ProportionalDock {
			Orientation      = Orientation.Horizontal,
			IsCollapsable    = false,
			VisibleDockables = CreateList<IDockable>(
				leftDock,
				new ProportionalDockSplitter(),
				documentDock,
				new ProportionalDockSplitter(),
				rightDock
			),
		};

		var windowLayout = CreateRootDock();
		windowLayout.Title           = "Default";
		windowLayout.IsCollapsable   = false;
		windowLayout.VisibleDockables = CreateList<IDockable>(mainLayout);
		windowLayout.ActiveDockable   = mainLayout;

		var root = CreateRootDock();
		root.IsCollapsable    = false;
		root.VisibleDockables = CreateList<IDockable>(windowLayout);
		root.ActiveDockable   = windowLayout;
		root.DefaultDockable  = windowLayout;

		m_rootDock     = root;
		m_documentDock = documentDock;
		return root;
	}

	public override void InitLayout(IDockable layout) {
		ContextLocator = new Dictionary<string, Func<object?>> {
			["Viewport"]  = () => layout,
			["Hierarchy"] = () => layout,
			["Inspector"] = () => layout,
		};
		DockableLocator = new Dictionary<string, Func<IDockable?>> {
			["Root"]      = () => m_rootDock,
			["Documents"] = () => m_documentDock,
		};
		HostWindowLocator = new Dictionary<string, Func<IHostWindow?>> {
			[nameof(IDockWindow)] = () => new HostWindow()
		};
		HideToolsOnClose = true;
		base.InitLayout(layout);
	}

	public override void CloseDockable(IDockable dockable) {
		if (dockable is null) return;
		base.CloseDockable(dockable);
	}

	public override IDock CreateSplitLayout(IDock dock, IDockable dockable, DockOperation operation) {
		var layout = base.CreateSplitLayout(dock, dockable, operation);
		var isTool = dockable is ITool or IToolDock;
		if (!isTool || layout.VisibleDockables == null) return layout;
		var proportion = operation is DockOperation.Left or DockOperation.Right ? 0.2 : 0.5;
		foreach (var child in layout.VisibleDockables) {
			if (child is not IDock childDock || childDock == dock) continue;
			childDock.Proportion = proportion;
			return layout;
		}
		return layout;
	}
}
