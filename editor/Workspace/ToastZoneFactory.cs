using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Dock.Avalonia.Controls;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Logger;

namespace editor.Workspace;

public class ToastZoneFactory : Factory {
	private IRootDock? m_rootDock;
	private ToolDock? m_toolDock;
	private bool m_curveClosePending;
	private bool m_hapticsClosePending;

	public LogsViewModel? LogsVm { get; private set; }
	public Editors.CurveViewModel? CurveEditorVm { get; private set; }
	public Editors.HapticsViewModel? HapticsEditorVm { get; private set; }

	public override IRootDock CreateLayout() {
		var assetBrowser = new AssetBrowserViewModel
			{ Id = "AssetBrowser", Title = "Asset Browser", CanPin = false, CanFloat = false, CanClose = false, CanDrag = false };
		var logs = new LogsViewModel { Id = "Logs", Title = "Logs", CanPin = false };
		var hapticsEditor = new Editors.HapticsViewModel
			{ Id = "Haptics", Title = "Haptics Editor", CanPin = false, CanFloat = false };
		var curveEditor = new Editors.CurveViewModel
			{ Id = "Curve", Title = "Curve Editor", CanPin = false, CanFloat = false };

		LogsVm = logs;
		HapticsEditorVm = hapticsEditor;
		CurveEditorVm = curveEditor;

		m_toolDock = new ToolDock {
			AllowedDropOperations = DockOperationMask.Fill | DockOperationMask.Left | DockOperationMask.Right,
			ActiveDockable = assetBrowser,
			VisibleDockables = CreateList<IDockable>(
				assetBrowser,
				logs,
				hapticsEditor,
				curveEditor
			),
			Alignment = Alignment.Bottom,
			GripMode = GripMode.Visible
		};

		var mainLayout = new ProportionalDock {
			Orientation = Orientation.Horizontal,
			IsCollapsable = false,
			VisibleDockables = CreateList<IDockable>(m_toolDock)
		};

		m_rootDock = CreateRootDock();
		m_rootDock.IsCollapsable = false;
		m_rootDock.VisibleDockables = CreateList<IDockable>(mainLayout);
		m_rootDock.ActiveDockable = mainLayout;
		m_rootDock.DefaultDockable = mainLayout;
		return m_rootDock;
	}

	// Brings a toast-zone tab to the front, re-adding it if it was closed
	public void ShowTool(Tool tool) {
		if (m_toolDock is null) return;
		if (m_toolDock.VisibleDockables?.Contains(tool) != true)
			AddDockable(m_toolDock, tool);
		SetActiveDockable(tool);
	}

	private void HideTool(Tool tool) {
		if (m_toolDock?.VisibleDockables?.Contains(tool) == true)
			CloseDockable(tool);
	}

	private Tool? ToolById(string id) => id switch {
		"Logs" => LogsVm,
		"Haptics" => HapticsEditorVm,
		"Curve" => CurveEditorVm,
		_ => null
	};

	public bool IsToolVisible(string id) =>
		ToolById(id) is { } tool && m_toolDock?.VisibleDockables?.Contains(tool) == true;

	public bool ToggleTool(string id) {
		if (ToolById(id) is not { } tool) return false;
		if (IsToolVisible(id)) { HideTool(tool); return false; }
		ShowTool(tool);
		return true;
	}

	public override void InitLayout(IDockable layout) {
		ContextLocator = new Dictionary<string, Func<object?>> {
			["AssetBrowser"] = () => layout,
			["Logs"] = () => layout,
			["Haptics"] = () => layout,
			["Curve"] = () => layout
		};
		DockableLocator = new Dictionary<string, Func<IDockable?>> {
			["Root"] = () => m_rootDock,
			["Haptics"] = () => HapticsEditorVm,
			["Curve"] = () => CurveEditorVm
		};
		HostWindowLocator = new Dictionary<string, Func<IHostWindow?>> {
			[nameof(IDockWindow)] = () => new HostWindow()
		};
		HideToolsOnClose = true;
		base.InitLayout(layout);
	}

	public override void CloseDockable(IDockable dockable) {
		if (dockable is null) return;

		if (dockable == CurveEditorVm && CurveEditorVm!.IsDirty && !m_curveClosePending) {
			_ = GatedClose(CurveEditorVm, CurveEditorVm, v => m_curveClosePending = v);
			return;
		}

		if (dockable == HapticsEditorVm && HapticsEditorVm!.IsDirty && !m_hapticsClosePending) {
			_ = GatedClose(HapticsEditorVm, HapticsEditorVm, v => m_hapticsClosePending = v);
			return;
		}

		base.CloseDockable(dockable);
	}

	private async Task GatedClose(Tool tool, Editors.IToastZoneEditor editor, Action<bool> setPending) {
		if (!await editor.ConfirmCloseCurrentAsync()) return;
		setPending(true);
		CloseDockable(tool);
		setPending(false);
	}
}
