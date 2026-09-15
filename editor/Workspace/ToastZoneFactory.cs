using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Editors;
using editor.Logger;

namespace editor.Workspace;

public class ToastZoneFactory : Factory {
	private bool m_curveClosePending;
	private bool m_hapticsClosePending;
	private IRootDock? m_rootDock;
	private bool m_tableClosePending;
	private IToolDock? m_toolDock;

	public LogsViewModel? LogsVm { get; private set; }
	public CurveViewModel? CurveEditorVm { get; private set; }
	public HapticsViewModel? HapticsEditorVm { get; private set; }
	public TableViewModel? TableEditorVm { get; private set; }
	public AssetBrowserViewModel? AssetBrowserVm { get; private set; }

	public override IRootDock CreateLayout() {
		var assetBrowser = new AssetBrowserViewModel {
			Id = "AssetBrowser", Title = "Asset Browser", CanPin = false, CanFloat = false, CanClose = false,
			CanDrag = false
		};
		var logs = new LogsViewModel { Id = "Logs", Title = "Logs", CanPin = false };
		var hapticsEditor = new HapticsViewModel
			{ Id = "Haptics", Title = "Haptics Editor", CanPin = false, CanFloat = false };
		var curveEditor = new CurveViewModel
			{ Id = "Curve", Title = "Curve Editor", CanPin = false, CanFloat = false };
		var tableEditor = new TableViewModel
			{ Id = "Table", Title = "Table Editor", CanPin = false, CanFloat = false };

		AssetBrowserVm = assetBrowser;
		LogsVm = logs;
		HapticsEditorVm = hapticsEditor;
		CurveEditorVm = curveEditor;
		TableEditorVm = tableEditor;

		m_toolDock = new ToolDock {
			AllowedDropOperations = DockOperationMask.Fill | DockOperationMask.Left | DockOperationMask.Right,
			ActiveDockable = assetBrowser,
			VisibleDockables = CreateList<IDockable>(
				assetBrowser,
				logs
			),
			Alignment = Alignment.Bottom,
			GripMode = GripMode.Hidden
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
		if (m_rootDock is null) return;
		if (LayoutSerializer.ContainsVisible(m_rootDock, tool)) {
			SetActiveDockable(tool);
			return;
		}

		var target = tool.OriginalOwner as IToolDock ?? m_toolDock;
		if (target is null || !LayoutSerializer.ContainsVisible(m_rootDock, target)) target = FirstToolDock();
		if (target is null) return;

		m_rootDock.HiddenDockables?.Remove(tool);
		AddDockable(target, tool);
		SetActiveDockable(tool);
	}

	private void HideTool(Tool tool) {
		if (LayoutSerializer.ContainsVisible(m_rootDock, tool)) CloseDockable(tool);
	}

	private IToolDock? FirstToolDock() {
		return LayoutSerializer.EnumerateDocks(m_rootDock, false).OfType<IToolDock>().FirstOrDefault();
	}

	public Tool? ToolById(string id) {
		return id switch {
			"Logs" => LogsVm,
			"Haptics" => HapticsEditorVm,
			"Curve" => CurveEditorVm,
			"Table" => TableEditorVm,
			_ => null
		};
	}

	public bool IsToolVisible(string id) {
		return ToolById(id) is { } tool && LayoutSerializer.ContainsVisible(m_rootDock, tool);
	}

	public LayoutNode? CaptureLayout() {
		return LayoutSerializer.Capture(m_rootDock, null);
	}

	public IRootDock? RebuildLayout(LayoutNode? node) {
		if (node is null) return null;

		var root = LayoutSerializer.BuildRoot(node, this, ResolveDockable);
		if (root is null) return null;

		foreach (var tool in AllTools())
			if (tool is not null)
				tool.Owner = null;

		m_rootDock = root;
		m_toolDock = FindBottomDock(root);
		EnsureAssetBrowser(root);
		return root;
	}

	private IDockable? ResolveDockable(string id) {
		return id == "AssetBrowser" ? AssetBrowserVm : ToolById(id);
	}

	private IEnumerable<Tool?> AllTools() {
		yield return AssetBrowserVm;
		yield return LogsVm;
		yield return HapticsEditorVm;
		yield return CurveEditorVm;
		yield return TableEditorVm;
	}

	private static IToolDock? FindBottomDock(IRootDock root) {
		var docks = LayoutSerializer.EnumerateDocks(root, false).OfType<IToolDock>().ToList();
		return docks.FirstOrDefault(d => d.Alignment == Alignment.Bottom) ?? docks.FirstOrDefault();
	}

	private void EnsureAssetBrowser(IRootDock root) {
		if (AssetBrowserVm is null || LayoutSerializer.ContainsVisible(root, AssetBrowserVm)) return;

		if (m_toolDock is null) {
			m_toolDock = CreateToolDock();
			m_toolDock.Alignment = Alignment.Bottom;
			m_toolDock.GripMode = GripMode.Hidden;
			m_toolDock.VisibleDockables = CreateList<IDockable>();
			root.VisibleDockables ??= CreateList<IDockable>();
			root.VisibleDockables.Add(m_toolDock);
		}

		m_toolDock.VisibleDockables ??= CreateList<IDockable>();
		m_toolDock.VisibleDockables.Insert(0, AssetBrowserVm);
		m_toolDock.ActiveDockable ??= AssetBrowserVm;
	}

	public bool ToggleTool(string id) {
		if (ToolById(id) is not { } tool) return false;
		if (IsToolVisible(id)) {
			HideTool(tool);
			return false;
		}

		ShowTool(tool);
		return true;
	}

	public override void InitLayout(IDockable layout) {
		ContextLocator = new Dictionary<string, Func<object?>> {
			["AssetBrowser"] = () => layout,
			["Logs"] = () => layout,
			["Haptics"] = () => layout,
			["Curve"] = () => layout,
			["Table"] = () => layout
		};
		DockableLocator = new Dictionary<string, Func<IDockable?>> {
			["Root"] = () => m_rootDock,
			["Haptics"] = () => HapticsEditorVm,
			["Curve"] = () => CurveEditorVm,
			["Table"] = () => TableEditorVm
		};
		HostWindowLocator = new Dictionary<string, Func<IHostWindow?>> {
			[nameof(IDockWindow)] = () => new EditorHostWindow()
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

		if (dockable == TableEditorVm && TableEditorVm!.IsDirty && !m_tableClosePending) {
			_ = GatedClose(TableEditorVm, TableEditorVm, v => m_tableClosePending = v);
			return;
		}

		base.CloseDockable(dockable);
	}

	private async Task GatedClose(Tool tool, IToastZoneEditor editor, Action<bool> setPending) {
		if (!await editor.ConfirmCloseCurrentAsync()) return;
		setPending(true);
		CloseDockable(tool);
		setPending(false);
	}
}
