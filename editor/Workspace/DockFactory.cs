using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Dock.Avalonia.Controls;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm;
using Dock.Model.Mvvm.Controls;
using editor.Assets.Types;
using editor.Editors;

namespace editor.Workspace;

public class DockFactory : Factory {
    private IDocumentDock? m_documentDock;
    private IRootDock? m_rootDock;
    private ToolDock? m_leftToolDock;
    private ToolDock? m_rightToolDock;

    private bool m_genericClosePending;
    private bool m_schemaClosePending;

    public HierarchyViewModel? Hierarchy { get; private set; }
    public InspectorViewModel? Inspector { get; private set; }
    public GenericViewModel? GenericEditorVm { get; private set; }
    public SchemaViewModel? SchemaEditorVm { get; private set; }

    public WorkspaceViewModel? ActiveWorkspace => m_documentDock?.ActiveDockable as WorkspaceViewModel;

    public override IRootDock CreateLayout() {
        var hierarchy = new HierarchyViewModel { Id = "Hierarchy", Title = "Hierarchy" };
        var inspector = new InspectorViewModel { Id = "Inspector", Title = "Inspector" };
        var generic   = new GenericViewModel { Id = "GenericEditor", Title = "Data Editor" };
        var schema    = new SchemaViewModel { Id = "SchemaEditor",  Title = "Schema Editor" };

        Hierarchy       = hierarchy;
        Inspector       = inspector;
        GenericEditorVm = generic;
        SchemaEditorVm  = schema;

        var documentDock = new DocumentDock {
            IsCollapsable = false,
            AllowedDropOperations = DockOperationMask.Left | DockOperationMask.Right,
            VisibleDockables = CreateList<IDockable>()
        };

        // left panel (hierarchy)
        var leftToolDock = new ToolDock {
            ActiveDockable = hierarchy,
            AllowedDropOperations = DockOperationMask.Fill | DockOperationMask.Top | DockOperationMask.Bottom,
            VisibleDockables = CreateList<IDockable>(hierarchy),
            Alignment = Alignment.Left,
            GripMode = GripMode.Visible
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
            VisibleDockables = CreateList<IDockable>(inspector),
            Alignment = Alignment.Right,
            GripMode = GripMode.Visible
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
            ["Workspace"]     = () => layout,
            ["Hierarchy"]     = () => layout,
            ["Inspector"]     = () => layout,
            ["GenericEditor"] = () => layout,
            ["SchemaEditor"]  = () => layout
        };
        DockableLocator = new Dictionary<string, Func<IDockable?>> {
            ["Root"]          = () => m_rootDock,
            ["Documents"]     = () => m_documentDock,
            ["GenericEditor"] = () => GenericEditorVm,
            ["SchemaEditor"]  = () => SchemaEditorVm
        };
        HostWindowLocator = new Dictionary<string, Func<IHostWindow?>> {
            [nameof(IDockWindow)] = () => new HostWindow()
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


    public WorkspaceViewModel AddWorkspace(WorkspaceViewModel workspace) {
        AddDockable(m_documentDock!, workspace);
        SetActiveDockable(workspace);
        return workspace;
    }

    private void ShowRightTool(Tool tool) {
        if (m_rightToolDock is null) return;
        var visible = m_rightToolDock.VisibleDockables;
        if (visible is null || !visible.Contains(tool))
            AddDockable(m_rightToolDock, tool);
        SetActiveDockable(tool);
    }

    private void HideRightTool(Tool tool) {
        if (m_rightToolDock?.VisibleDockables?.Contains(tool) == true)
            CloseDockable(tool);
    }

    public bool IsRightToolVisible(Tool? tool) =>
        tool is not null && m_rightToolDock?.VisibleDockables?.Contains(tool) == true;

    private void ShowLeftTool(Tool tool) {
        if (m_leftToolDock is null) return;
        var visible = m_leftToolDock.VisibleDockables;
        if (visible is null || !visible.Contains(tool))
            AddDockable(m_leftToolDock, tool);
        SetActiveDockable(tool);
    }

    private void HideLeftTool(Tool tool) {
        if (m_leftToolDock?.VisibleDockables?.Contains(tool) == true)
            CloseDockable(tool);
    }

    public bool IsLeftToolVisible(Tool? tool) =>
        tool is not null && m_leftToolDock?.VisibleDockables?.Contains(tool) == true;

    public bool ToggleTool(string id) {
        switch (id) {
            case "Hierarchy" when Hierarchy is not null:
                if (IsLeftToolVisible(Hierarchy)) { HideLeftTool(Hierarchy);  return false; }
                else                              { ShowLeftTool(Hierarchy);   return true;  }

            case "Inspector" when Inspector is not null:
                if (IsRightToolVisible(Inspector)) { HideRightTool(Inspector);  return false; }
                else                               { ShowRightTool(Inspector);   return true;  }

            case "GenericEditor" when GenericEditorVm is not null:
                if (IsRightToolVisible(GenericEditorVm)) { HideRightTool(GenericEditorVm);  return false; }
                else                                     { ShowRightTool(GenericEditorVm);   return true;  }

            case "SchemaEditor" when SchemaEditorVm is not null:
                if (IsRightToolVisible(SchemaEditorVm)) { HideRightTool(SchemaEditorVm);  return false; }
                else                                    { ShowRightTool(SchemaEditorVm);   return true;  }
        }
        return false;
    }

    public bool IsToolVisible(string id) => id switch {
        "Hierarchy"    => IsLeftToolVisible(Hierarchy),
        "Inspector"    => IsRightToolVisible(Inspector),
        "GenericEditor"=> IsRightToolVisible(GenericEditorVm),
        "SchemaEditor" => IsRightToolVisible(SchemaEditorVm),
        _              => false
    };

    public void OpenGenericEditor(string uid, string virtualPath, BaseAsset definition) {
        if (GenericEditorVm!.IsDirty)
            _ = OpenGenericEditorGated(uid, virtualPath, definition);
        else
            DoOpenGenericEditor(uid, virtualPath, definition);
    }

    private void DoOpenGenericEditor(string uid, string virtualPath, BaseAsset definition) {
        GenericEditorVm!.OpenFile(uid, virtualPath, definition);
        ShowRightTool(GenericEditorVm);
    }

    private async Task OpenGenericEditorGated(string uid, string virtualPath, BaseAsset definition) {
        if (await GenericEditorVm!.ConfirmCloseCurrentAsync())
            DoOpenGenericEditor(uid, virtualPath, definition);
    }

    public void OpenSchemaEditor(string uid, string virtualPath) {
        if (SchemaEditorVm!.IsDirty)
            _ = OpenSchemaEditorGated(uid, virtualPath);
        else
            DoOpenSchemaEditor(uid, virtualPath);
    }

    private void DoOpenSchemaEditor(string uid, string virtualPath) {
        SchemaEditorVm!.OpenFile(uid, virtualPath);
        ShowRightTool(SchemaEditorVm);
    }

    private async Task OpenSchemaEditorGated(string uid, string virtualPath) {
        if (await SchemaEditorVm!.ConfirmCloseCurrentAsync())
            DoOpenSchemaEditor(uid, virtualPath);
    }
}
