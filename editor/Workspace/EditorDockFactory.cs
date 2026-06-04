//
// EditorDockFactory.cs by Xein
// 4 Jun 2026
//

using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Threading;
using Dock.Avalonia.Controls;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;

namespace editor.Workspace;

public class EditorDockFactory : Factory {
	private readonly ToastEngine m_toast;

	public HierarchyViewModel Hierarchy { get; } = new();
	public InspectorViewModel Inspector { get; } = new();

	/// @brief The scene document currently selected/focused
	public ViewportViewModel? ActiveScene { get; private set; }
	public event Action<ViewportViewModel?>? ActiveSceneChanged;

	/// @brief Raised when a tool is hidden or restored
	public event Action<IDockable>? DockableHidden;
	public event Action<IDockable>? DockableShown;

	private IRootDock m_root = null!;
	private ProportionalDock m_mainLayout = null!;
	private ProportionalDock m_leftPane = null!;
	private ProportionalDock m_rightPane = null!;
	private ToolDock m_leftTools = null!;
	private ToolDock m_rightTools = null!;
	private DocumentDock m_documentDock = null!;
	private ProportionalDockSplitter m_leftSplitter = null!;
	private ProportionalDockSplitter m_rightSplitter = null!;

	private bool m_closingFloat; // re-entrancy guard for floating-window teardown

	public EditorDockFactory(ToastEngine toast) {
		m_toast = toast;
		HideToolsOnClose = true;
	}

	public override IRootDock CreateLayout() {
		var scene1 = new ViewportViewModel("Scene 1", m_toast);

		m_leftTools = new ToolDock {
			Id = "LeftTools",
			Alignment = Alignment.Left,
			ActiveDockable = Hierarchy,
			AllowedDropOperations = DockOperationMask.Fill,
			VisibleDockables = CreateList<IDockable>(Hierarchy)
		};
		m_leftPane = new ProportionalDock {
			Id = "LeftPane",
			Proportion = SideProportion,
			Orientation = Orientation.Vertical,
			VisibleDockables = CreateList<IDockable>(m_leftTools)
		};

		m_documentDock = new DocumentDock {
			Id = "Scenes",
			IsCollapsable = false,
			Proportion = double.NaN,
			CanCreateDocument = false,
			EnableWindowDrag = true,
			ActiveDockable = scene1,
			AllowedDropOperations = DockOperationMask.Fill | DockOperationMask.Left | DockOperationMask.Right,
			VisibleDockables = CreateList<IDockable>(scene1)
		};

		m_rightTools = new ToolDock {
			Id = "RightTools",
			Alignment = Alignment.Right,
			ActiveDockable = Inspector,
			AllowedDropOperations = DockOperationMask.Fill,
			VisibleDockables = CreateList<IDockable>(Inspector)
		};
		m_rightPane = new ProportionalDock {
			Id = "RightPane",
			Proportion = SideProportion,
			Orientation = Orientation.Vertical,
			VisibleDockables = CreateList<IDockable>(m_rightTools)
		};

		m_leftSplitter = new ProportionalDockSplitter();
		m_rightSplitter = new ProportionalDockSplitter();

		m_mainLayout = new ProportionalDock {
			Id = "MainLayout",
			Orientation = Orientation.Horizontal,
			IsCollapsable = false,
			VisibleDockables = CreateList<IDockable>(
				m_leftPane,
				m_leftSplitter,
				m_documentDock,
				m_rightSplitter,
				m_rightPane
			)
		};

		var rootDock = CreateRootDock();
		rootDock.IsCollapsable = false;
		rootDock.ActiveDockable = m_mainLayout;
		rootDock.DefaultDockable = m_mainLayout;
		rootDock.VisibleDockables = CreateList<IDockable>(m_mainLayout);

		rootDock.LeftPinnedDockables   = CreateList<IDockable>();
		rootDock.RightPinnedDockables  = CreateList<IDockable>();
		rootDock.TopPinnedDockables    = CreateList<IDockable>();
		rootDock.BottomPinnedDockables = CreateList<IDockable>();
		rootDock.HiddenDockables       = CreateList<IDockable>();
		rootDock.PinnedDock = null;

		m_root = rootDock;
		return rootDock;
	}

	public override void InitLayout(IDockable layout) {
		ContextLocator = new Dictionary<string, Func<object?>> {
			["MainLayout"] = () => layout,
			["LeftPane"]   = () => layout,
			["LeftTools"]  = () => layout,
			["Scenes"]     = () => layout,
			["RightPane"]  = () => layout,
			["RightTools"] = () => layout
		};

		HostWindowLocator = new Dictionary<string, Func<IHostWindow?>> {
			[nameof(IDockWindow)] = () => new HostWindow {
				IsToolWindow = true,
				ToolChromeControlsWholeWindow = true
			}
		};
		DefaultHostWindowLocator = () => new HostWindow {
			IsToolWindow = true,
			ToolChromeControlsWholeWindow = true
		};

		base.InitLayout(layout);
	}

	public bool IsHidden(IDockable tool) =>
		m_root?.HiddenDockables?.Contains(tool) ?? false;

	/// @brief Proportion a tool panel gets when docked to a side
	private const double SideProportion = 0.2;

	public override void CloseDockable(IDockable dockable) {
		var root = ResolveRoot(dockable);

		if (root is not null && !ReferenceEquals(root, m_root)) {
			if (m_closingFloat)
				return;

			m_closingFloat = true;
			try {
				foreach (var t in CollectTools(root).ToList())
					HideTool(t);
				CloseWindowFor(root);
			} finally {
				m_closingFloat = false;
			}
			return;
		}

		if (dockable is ITool tool && IsManagedTool(tool)) {
			HideTool(tool);
			return;
		}

		if (dockable is null)
			return;

		base.CloseDockable(dockable);
	}

	public override void RestoreDockable(IDockable dockable) {
		if (dockable is ITool tool && IsManagedTool(tool)) {
			ShowTool(tool);
			return;
		}

		base.RestoreDockable(dockable);
	}

	private void HideTool(ITool tool) {
		var (pane, home) = HomeFor(tool);

		if (tool.Owner is IDock)
			RemoveDockable(tool, false);

		tool.Owner = home;
		tool.OriginalOwner = home;

		m_root.HiddenDockables ??= CreateList<IDockable>();
		if (!m_root.HiddenDockables.Contains(tool))
			m_root.HiddenDockables.Add(tool);

		CollapsePane(pane);
		DockableHidden?.Invoke(tool);
	}

	private void ShowTool(ITool tool) {
		var (pane, home) = HomeFor(tool);

		m_root.HiddenDockables?.Remove(tool);

		home.VisibleDockables ??= CreateList<IDockable>();
		if (!home.VisibleDockables.Contains(tool)) {
			tool.Owner = home;
			AddDockable(home, tool);
		}
		home.ActiveDockable = tool;

		pane.VisibleDockables ??= CreateList<IDockable>();
		if (!pane.VisibleDockables.Contains(home)) {
			home.Owner = pane;
			AddDockable(pane, home);
		}

		if (m_mainLayout.VisibleDockables?.Contains(pane) != true)
			InsertPaneIntoMain(pane);

		SetActiveDockable(tool);
		DockableShown?.Invoke(tool);
	}

	private void CloseWindowFor(IRootDock floatRoot) {
		var win = floatRoot.Window;
		if (win is null && m_root.Windows is { } windows)
			win = windows.FirstOrDefault(w => ReferenceEquals(w.Layout, floatRoot));
		win?.Exit();
	}

	public override void OnActiveDockableChanged(IDockable? dockable) {
		base.OnActiveDockableChanged(dockable);

		// Only react to scene documents; ignore tool focus changes
		if (dockable is ViewportViewModel scene) {
			ActiveScene = scene;
			ActiveSceneChanged?.Invoke(scene);
			// TODO(engine): when the C++ side supports multiple scenes, notify it here
			// e.g. scene.Engine?.SetActiveScene(scene.Id);
		}
	}

	public override void OnDockableDocked(IDockable? dockable, DockOperation operation) {
		base.OnDockableDocked(dockable, operation);

		if (dockable is IToolDock toolDock
		    && (operation == DockOperation.Left || operation == DockOperation.Right)) {
			toolDock.Proportion = SideProportion;
			Dispatcher.UIThread.Post(() => toolDock.Proportion = SideProportion);
		}
	}


	private bool IsManagedTool(IDockable dockable) =>
		ReferenceEquals(dockable, Hierarchy) || ReferenceEquals(dockable, Inspector);

	private (ProportionalDock pane, ToolDock home) HomeFor(IDockable tool) =>
		ReferenceEquals(tool, Inspector) ? (m_rightPane, m_rightTools) : (m_leftPane, m_leftTools);

	private void CollapsePane(ProportionalDock pane) {
		var splitter = ReferenceEquals(pane, m_leftPane) ? m_leftSplitter : m_rightSplitter;
		if (m_mainLayout.VisibleDockables?.Contains(pane) == true)
			RemoveDockable(pane, false);
		if (m_mainLayout.VisibleDockables?.Contains(splitter) == true)
			RemoveDockable(splitter, false);
	}

	private void InsertPaneIntoMain(ProportionalDock pane) {
		pane.Owner = m_mainLayout;
		pane.Proportion = SideProportion;

		if (ReferenceEquals(pane, m_leftPane)) {
			// Left edge: [leftPane, leftSplitter, document, ...]
			InsertDockable(m_mainLayout, m_leftPane, 0);
			InsertDockable(m_mainLayout, m_leftSplitter, 1);
		} else {
			// Right edge: [..., document, rightSplitter, rightPane]
			var count = m_mainLayout.VisibleDockables?.Count ?? 0;
			InsertDockable(m_mainLayout, m_rightSplitter, count);
			InsertDockable(m_mainLayout, m_rightPane, count + 1);
		}
	}

	private static IRootDock? ResolveRoot(IDockable? dockable) {
		var seen = new HashSet<IDockable>();
		var current = dockable;
		while (current is not null && seen.Add(current)) {
			if (current is IRootDock root)
				return root;
			current = current.Owner;
		}
		return null;
	}

	private static IEnumerable<ITool> CollectTools(IDockable dockable) {
		if (dockable is ITool tool)
			yield return tool;
		if (dockable is IDock dock && dock.VisibleDockables is { } children)
			foreach (var child in children)
				foreach (var nested in CollectTools(child))
					yield return nested;
	}
}
