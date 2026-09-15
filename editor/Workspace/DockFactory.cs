using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;
using editor.Assets.Types;
using editor.Editors;

namespace editor.Workspace;

public class DockFactory : Factory {
	private const double SidePanelProportion = 0.2;
	private IDocumentDock? m_documentDock;

	private bool m_genericClosePending;
	private IToolDock? m_leftToolDock;
	private IToolDock? m_rightToolDock;
	private IRootDock? m_rootDock;
	private bool m_schemaClosePending;

	public HierarchyViewModel? Hierarchy { get; private set; }
	public HistoryViewModel? History { get; private set; }
	public InspectorViewModel? Inspector { get; private set; }
	public SignalsViewModel? Signals { get; private set; }
	public GenericViewModel? GenericEditorVm { get; private set; }
	public SchemaViewModel? SchemaEditorVm { get; private set; }

	public WorkspaceViewModel? ActiveWorkspace => m_documentDock?.ActiveDockable as WorkspaceViewModel;

	public override IRootDock CreateLayout() {
		var hierarchy = new HierarchyViewModel { Id = "Hierarchy", Title = "Hierarchy" };
		var history = new HistoryViewModel { Id = "History", Title = "History" };
		var inspector = new InspectorViewModel { Id = "Inspector", Title = "Inspector" };
		var signals = new SignalsViewModel { Id = "Signals", Title = "Signals" };
		var generic = new GenericViewModel { Id = "GenericEditor", Title = "Data Editor" };
		var schema = new SchemaViewModel { Id = "SchemaEditor", Title = "Schema Editor" };

		Hierarchy = hierarchy;
		History = history;
		Inspector = inspector;
		Signals = signals;
		GenericEditorVm = generic;
		SchemaEditorVm = schema;

		var documentDock = new DocumentDock {
			IsCollapsable = false,
			AllowedDropOperations = DockOperationMask.Fill,
			VisibleDockables = CreateList<IDockable>()
		};

		// left panel (hierarchy)
		var leftToolDock = new ToolDock {
			ActiveDockable = hierarchy,
			AllowedDropOperations = DockOperationMask.Fill | DockOperationMask.Top | DockOperationMask.Bottom,
			VisibleDockables = CreateList<IDockable>(hierarchy),
			Alignment = Alignment.Left,
			GripMode = GripMode.Hidden
		};
		m_leftToolDock = leftToolDock;

		var leftDock = new ProportionalDock {
			Proportion = 0.2,
			AllowedDropOperations = DockOperationMask.None,
			Orientation = Orientation.Vertical,
			VisibleDockables = CreateList<IDockable>(leftToolDock)
		};

		// right panel (inspector)
		var rightToolDock = new ToolDock {
			ActiveDockable = inspector,
			VisibleDockables = CreateList<IDockable>(inspector, signals),
			Alignment = Alignment.Right,
			GripMode = GripMode.Hidden
		};
		m_rightToolDock = rightToolDock;

		var rightDock = new ProportionalDock {
			Proportion = 0.22,
			Orientation = Orientation.Vertical,
			VisibleDockables = CreateList<IDockable>(rightToolDock)
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
			["History"] = () => layout,
			["Inspector"] = () => layout,
			["Signals"] = () => layout,
			["GenericEditor"] = () => layout,
			["SchemaEditor"] = () => layout
		};
		DockableLocator = new Dictionary<string, Func<IDockable?>> {
			["Root"] = () => m_rootDock,
			["Documents"] = () => m_documentDock,
			["History"] = () => History,
			["GenericEditor"] = () => GenericEditorVm,
			["SchemaEditor"] = () => SchemaEditorVm
		};
		HostWindowLocator = new Dictionary<string, Func<IHostWindow?>> {
			[nameof(IDockWindow)] = () => new EditorHostWindow()
		};
		HideToolsOnClose = true;
		base.InitLayout(layout);
	}

	public override void CloseDockable(IDockable dockable) {
		if (dockable is null) return;

		if (dockable is WorkspaceViewModel ws && !ws.PendingClose) {
			_ = GatedClose(ws);
			return;
		}

		if (dockable == GenericEditorVm && GenericEditorVm!.IsDirty && !m_genericClosePending) {
			_ = GatedCloseGeneric();
			return;
		}

		if (dockable == SchemaEditorVm && SchemaEditorVm!.IsDirty && !m_schemaClosePending) {
			_ = GatedCloseSchema();
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

	private async Task GatedCloseGeneric() {
		if (await GenericEditorVm!.ConfirmCloseCurrentAsync()) {
			m_genericClosePending = true;
			CloseDockable(GenericEditorVm);
			m_genericClosePending = false;
		}
	}

	private async Task GatedCloseSchema() {
		if (await SchemaEditorVm!.ConfirmCloseCurrentAsync()) {
			m_schemaClosePending = true;
			CloseDockable(SchemaEditorVm);
			m_schemaClosePending = false;
		}
	}

	/**
	 * Re-applies the side-panel share after a dock completes
	 */
	public override void OnDockableDocked(IDockable dockable, DockOperation operation) {
		base.OnDockableDocked(dockable, operation);
		if (operation is not (DockOperation.Left or DockOperation.Right)) return;
		if (dockable is IDocument or IDocumentDock) return;

		// walk out to the node sitting directly inside a horizontal dock
		for (var current = dockable; current is not null; current = current.Owner) {
			if (current.Owner is not IProportionalDock { Orientation: Orientation.Horizontal } parent) continue;
			if (parent.VisibleDockables?.Contains(current) != true) continue;
			current.Proportion = SidePanelProportion;
			return;
		}
	}

	public override IDock CreateSplitLayout(IDock dock, IDockable dockable, DockOperation operation) {
		var layout = base.CreateSplitLayout(dock, dockable, operation);

		var isDocument = dockable is IDocument or IDocumentDock;
		if (isDocument || layout.VisibleDockables == null) return layout;
		var proportion = operation is DockOperation.Left or DockOperation.Right ? SidePanelProportion : 0.5;
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

	public LayoutNode? CaptureLayout() {
		return LayoutSerializer.Capture(m_rootDock, m_documentDock);
	}

	public IRootDock? RebuildLayout(LayoutNode? node) {
		if (node is null || m_documentDock is null) return null;

		var root = LayoutSerializer.BuildRoot(node, this, id => ToolById(id), m_documentDock, ConfigureToolDock);
		// nothing is mutated until the build succeeds
		if (root is null) return null;

		// tools left out of the new tree must not keep pointing into the one being discarded
		foreach (var tool in AllTools())
			if (tool is not null)
				tool.Owner = null;

		EnsureDocumentDock(root);
		m_rootDock = root;
		m_leftToolDock = FindToolDock(root, Alignment.Left) ?? FindOwnerToolDock(root, Hierarchy);
		m_rightToolDock = FindToolDock(root, Alignment.Right) ?? FindOwnerToolDock(root, Inspector);
		return root;
	}

	private static void ConfigureToolDock(IToolDock dock) {
		if (dock is IDockableDockingRestrictions restrictions && dock.Alignment == Alignment.Left)
			restrictions.AllowedDropOperations =
				DockOperationMask.Fill | DockOperationMask.Top | DockOperationMask.Bottom;
	}

	private void EnsureDocumentDock(IRootDock root) {
		if (m_documentDock is null || LayoutSerializer.ContainsVisible(root, m_documentDock)) return;

		var host = LayoutSerializer.EnumerateDocks(root, false)
			.OfType<IProportionalDock>()
			.FirstOrDefault(d => d.Orientation == Orientation.Horizontal);

		if (host is null) {
			host = CreateProportionalDock();
			host.Orientation = Orientation.Horizontal;
			host.IsCollapsable = false;
			host.VisibleDockables = CreateList<IDockable>(m_documentDock);
			root.VisibleDockables = CreateList<IDockable>(host);
			root.DefaultDockable = host;
			root.ActiveDockable = host;
			return;
		}

		host.VisibleDockables ??= CreateList<IDockable>();
		if (host.VisibleDockables.Count > 0) host.VisibleDockables.Add(new ProportionalDockSplitter());
		host.VisibleDockables.Add(m_documentDock);
	}

	private static IToolDock? FindToolDock(IRootDock root, Alignment alignment) {
		return LayoutSerializer.EnumerateDocks(root, false)
			.OfType<IToolDock>()
			.FirstOrDefault(d => d.Alignment == alignment);
	}

	private static IToolDock? FindOwnerToolDock(IRootDock root, Tool? tool) {
		if (tool is null) return null;
		return LayoutSerializer.EnumerateDocks(root, false)
			.OfType<IToolDock>()
			.FirstOrDefault(d => d.VisibleDockables?.Contains(tool) == true);
	}

	public Tool? ToolById(string id) {
		return id switch {
			"Hierarchy" => Hierarchy,
			"History" => History,
			"Inspector" => Inspector,
			"Signals" => Signals,
			"GenericEditor" => GenericEditorVm,
			"SchemaEditor" => SchemaEditorVm,
			_ => null
		};
	}

	private IEnumerable<Tool?> AllTools() {
		yield return Hierarchy;
		yield return History;
		yield return Inspector;
		yield return Signals;
		yield return GenericEditorVm;
		yield return SchemaEditorVm;
	}

	private IToolDock? PreferredDockFor(Tool tool) {
		return ReferenceEquals(tool, Hierarchy) || ReferenceEquals(tool, History) ? m_leftToolDock : m_rightToolDock;
	}

	private void ShowTool(Tool tool, IToolDock? preferred) {
		if (m_rootDock is null) return;
		if (LayoutSerializer.ContainsVisible(m_rootDock, tool)) {
			SetActiveDockable(tool);
			return;
		}

		var target = PickToolDock(tool, preferred);
		if (target is null) return;

		m_rootDock.HiddenDockables?.Remove(tool);
		AddDockable(target, tool);
		SetActiveDockable(tool);
	}

	// re-open where the user last had it
	private IToolDock? PickToolDock(Tool tool, IToolDock? preferred) {
		if (tool.OriginalOwner is IToolDock original && IsInTree(original)) return original;
		if (preferred is not null && IsInTree(preferred)) return preferred;
		return LayoutSerializer.EnumerateDocks(m_rootDock, false).OfType<IToolDock>().FirstOrDefault();
	}

	private void ShowRightTool(Tool tool) {
		ShowTool(tool, m_rightToolDock);
	}

	private void HideTool(Tool tool) {
		if (LayoutSerializer.ContainsVisible(m_rootDock, tool)) CloseDockable(tool);
	}

	private bool IsInTree(IDock dock) {
		return ReferenceEquals(dock, m_rootDock) || LayoutSerializer.ContainsVisible(m_rootDock, dock);
	}

	public bool ToggleTool(string id) {
		if (ToolById(id) is not { } tool) return false;
		if (IsToolVisible(id)) {
			HideTool(tool);
			return false;
		}

		ShowTool(tool, PreferredDockFor(tool));
		return true;
	}


	public bool IsToolVisible(string id) {
		return ToolById(id) is { } tool && LayoutSerializer.ContainsVisible(m_rootDock, tool);
	}

	public void OpenGenericEditor(
		string uid, string virtualPath, BaseAsset definition, string? contentSourceRealPath = null) {
		if (GenericEditorVm!.IsDirty)
			_ = OpenGenericEditorGated(uid, virtualPath, definition, contentSourceRealPath);
		else
			DoOpenGenericEditor(uid, virtualPath, definition, contentSourceRealPath);
	}

	private void DoOpenGenericEditor(
		string uid, string virtualPath, BaseAsset definition, string? contentSourceRealPath) {
		GenericEditorVm!.OpenFile(uid, virtualPath, definition, contentSourceRealPath);
		ShowRightTool(GenericEditorVm);
	}

	private async Task OpenGenericEditorGated(
		string uid, string virtualPath, BaseAsset definition, string? contentSourceRealPath) {
		if (await GenericEditorVm!.ConfirmCloseCurrentAsync())
			DoOpenGenericEditor(uid, virtualPath, definition, contentSourceRealPath);
	}

	public void OpenSchemaEditor(string uid, string virtualPath, string? contentSourceRealPath = null) {
		if (SchemaEditorVm!.IsDirty)
			_ = OpenSchemaEditorGated(uid, virtualPath, contentSourceRealPath);
		else
			DoOpenSchemaEditor(uid, virtualPath, contentSourceRealPath);
	}

	private void DoOpenSchemaEditor(string uid, string virtualPath, string? contentSourceRealPath) {
		SchemaEditorVm!.OpenFile(uid, virtualPath, contentSourceRealPath);
		ShowRightTool(SchemaEditorVm);
	}

	private async Task OpenSchemaEditorGated(string uid, string virtualPath, string? contentSourceRealPath) {
		if (await SchemaEditorVm!.ConfirmCloseCurrentAsync())
			DoOpenSchemaEditor(uid, virtualPath, contentSourceRealPath);
	}
}
