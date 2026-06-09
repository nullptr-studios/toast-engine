using System;
using System.Collections.Generic;
using Dock.Avalonia.Controls;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;
using editor.AssetBrowser;
using editor.Logger;

namespace editor.Workspace;

public class ToastZoneFactory : Factory {
	private IRootDock? m_rootDock;

	public override IRootDock CreateLayout() {
		var assetBrowser = new AssetBrowserViewModel { Id = "AssetBrowser", Title = "Asset Browser", CanPin = false, CanFloat = false };
		var logs = new LogsViewModel {  Id = "Logs", Title = "Logs", CanPin = false };

		var mainLayout = new ProportionalDock {
			Orientation      = Orientation.Horizontal,
			IsCollapsable    = false,
			VisibleDockables = CreateList<IDockable>(
				new ToolDock {
					AllowedDropOperations = DockOperationMask.Fill | DockOperationMask.Left | DockOperationMask.Right,
					ActiveDockable        = assetBrowser,
					VisibleDockables      = CreateList<IDockable>(
						assetBrowser,
						logs
					),
					Alignment             = Alignment.Bottom,
					GripMode              = GripMode.Visible,
				}
			),
		};

		m_rootDock = CreateRootDock();
		m_rootDock.IsCollapsable    = false;
		m_rootDock.VisibleDockables = CreateList<IDockable>(mainLayout);
		m_rootDock.ActiveDockable   = mainLayout;
		m_rootDock.DefaultDockable  = mainLayout;
		return m_rootDock;
	}

	public override void InitLayout(IDockable layout) {
		ContextLocator = new Dictionary<string, Func<object?>> {
			["AssetBrowser"] = () => layout,
			["Logs"] = () => layout,
		};
		DockableLocator = new Dictionary<string, Func<IDockable?>> {
			["Root"] = () => m_rootDock,
		};
		HostWindowLocator = new Dictionary<string, Func<IHostWindow?>> {
			[nameof(IDockWindow)] = () => new HostWindow(),
		};
		HideToolsOnClose = true;
		base.InitLayout(layout);
	}

	public override void CloseDockable(IDockable dockable) {
		if (dockable is null) return;
		base.CloseDockable(dockable);
	}
}
