using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.Assets;
using editor.Engine;
using editor.Workspace;
using Proto.Events;
using HierarchyElement = editor.Workspace.HierarchyElement;

namespace editor.VoxelEditor;

public enum VoxelTool { Select, Move, Extrude, Buildup, Carve, Paint, Bucket, Split, Slice }

public enum VoxelViewMode { Lit, Albedo, MetallicRoughness, PaletteId, PhysicsMaterial, DdaSteps }

public sealed record VoxelPieceInfo(
	string Uid,
	string Name,
	uint Kind,
	(int X, int Y, int Z) Min,
	(int X, int Y, int Z) Max,
	Color Color,
	IReadOnlyList<(double X, double Y, double Z, double W)> Planes,
	bool Resizable,
	int ColorId) {
	public int Axis(int axis, bool max) {
		var v = max ? Max : Min;
		return axis switch { 0 => v.X, 1 => v.Y, _ => v.Z };
	}
}

public sealed record VoxelProjectionInfo(
	int MinH, int MinV, int Width, int Height, byte[] Colors, short[] Depths, byte[] EdgesH, byte[] EdgesV) {
	public bool Solid(int h, int v) {
		return h >= 0 && v >= 0 && h < Width && v < Height && Colors[(h + v * Width) * 4 + 3] != 0;
	}

	public short Depth(int h, int v) {
		return Depths[h + v * Width];
	}
}

public partial class VoxelEditorViewModel : ObservableObject, IDisposable {
	// Snapping in the VoxelEditor is always one voxel and a right angle
	public const double VoxelSize = 0.1;
	public const double RightAngle = 90.0;

	public const string DrawBoxScript = "core://voxel_scripts/draw_box.lua";

	private static ulong s_nextTransaction = 1 << 20;

	private readonly Listener m_listener = new();
	private readonly bool m_ownsWorkspace;
	private bool m_disposed;
	private ulong m_transaction;
	private bool m_applyingSelection;

	[ObservableProperty] private VoxelTool m_activeTool = VoxelTool.Select;
	[ObservableProperty] private VoxelViewMode m_viewMode = VoxelViewMode.Lit;
	[ObservableProperty] private bool m_showUnitGrid;
	[ObservableProperty] private bool m_showVoxelGrid;
	[ObservableProperty] private bool m_showVoxelEdges = true;
	[ObservableProperty] private bool m_showEdges;
	[ObservableProperty] private bool m_fourUp;
	[ObservableProperty] private bool m_showOthers;
	[ObservableProperty] private int m_paletteId = 1;
	[ObservableProperty] private string? m_defaultScript;
	[ObservableProperty] private string m_defaultFillModeName = "Replace";
	public static IReadOnlyList<string> FillModes { get; } = ["Replace", "Empty Only", "Solid Only", "Match"];
	private uint DefaultFillMode => (uint)Math.Max(0, FillModes.ToList().IndexOf(DefaultFillModeName));

	public VoxelEditorViewModel(
		WorkspaceViewModel workspace, bool ownsWorkspace, string prefabUid, ulong sourceWorkspace = 0, string sourceInstance = "") {
		Workspace = workspace;
		PrefabUid = prefabUid;
		SourceWorkspace = sourceWorkspace;
		SourceInstance = sourceInstance;
		m_ownsWorkspace = ownsWorkspace;
		DefaultScript = ResolveUid(DrawBoxScript);

		Hierarchy = new HierarchyViewModel(() => Workspace) { Id = "VoxelHierarchy", Title = "Volumes" };
		Hierarchy.CanContextBake = CanBakeFromHierarchy;
		Hierarchy.ContextBake = BakeFromHierarchy;
		Inspector = new InspectorViewModel(Hierarchy) {
			Id = "VoxelInspector", Title = "Inspector", Profile = new InspectorProfile(SectionsFor, true)
		};
		Hierarchy.SelectedChanged += _ => OnSelectionChanged();
		Hierarchy.HierarchyChanged += OnHierarchyChanged;
		Workspace.PropertyChanged += OnWorkspaceChanged;

		m_listener.SubscribeOnUiThread<ProceduralVoxelLayout>(OnLayout);
		m_listener.SubscribeOnUiThread<NodePicked>(OnNodePicked);
	}

	public WorkspaceViewModel Workspace { get; }
	public HierarchyViewModel Hierarchy { get; }
	public InspectorViewModel Inspector { get; }
	public string PrefabUid { get; }
	public ulong SourceWorkspace { get; }
	public string SourceInstance { get; }
	public bool CanShowOthers => SourceWorkspace != 0;

	public string Title => $"Voxel Editor - {Workspace.Title}{(Workspace.IsModified ? " *" : "")}";

	public IReadOnlyList<VoxelPieceInfo> Pieces { get; private set; } = [];

	// Front, side and top, empty until the 2D views ask for them
	public IReadOnlyList<VoxelProjectionInfo> Projections { get; private set; } = [];

	// The palette asset the shape uses, empty for the engine default
	public string PaletteUid { get; private set; } = "";

	public string? SelectedUid => Hierarchy.SelectedNode?.Uid;
	public VoxelPieceInfo? SelectedPiece => Pieces.FirstOrDefault(p => p.Uid == SelectedUid);

	private bool OwnsEngine => ViewportFocus.DetachedOwner == Workspace.EffectiveHandle && !m_disposed;

	public event Action? LayoutChanged;
	public event Action? SelectionChanged;

	// We don't put every element from the inspector here
	// We just put the things we care about on this view
	private static IReadOnlyList<InspectorSection>? SectionsFor(string type) {
		const string green = "Green";
		return type switch {
			"FillVolume" or "PaintVolume" => [
				new InspectorSection("Volume", green, "BoxMesh", ["position", "size", "mode", "match_id"]),
				new InspectorSection("Script", "Magenta", "Circle", ["m_shape_script"])
			],
			"CarveVolume" => [
				new InspectorSection("Volume", green, "BoxOccluder", ["position", "size"]),
				new InspectorSection("Script", "Magenta", "Circle", ["m_shape_script"])
			],
			"VoxelMesh" => [
				new InspectorSection("Position", green, "Move3d", ["position"]),
				new InspectorSection("Model", green, "MeshItem", ["m_model"]),
				new InspectorSection("Script", "Magenta", "Circle", ["m_shape_script"])
			],
			"ProceduralVoxel" => [
				new InspectorSection("Shape", green, "VoxelGI", ["m_palette"]),
				new InspectorSection("Physics", "Orange", "PhysicsBody", ["simulation_type", "mass", "group:Physics"])
			],
			"VoxelGroup" => [new InspectorSection("Group", green, "Container", ["position"])],
			"VoxelBucket" => [new InspectorSection("Bucket", green, "Bucket", ["position", "id"])],
			_ => null
		};
	}

	private static string? ResolveUid(string virtualPath) {
		var real = ProjectContext.Resolve(virtualPath);
		return MetaFile.ReadHeader(real)?.Uid;
	}

	private void OnWorkspaceChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName is nameof(WorkspaceViewModel.IsModified) or nameof(WorkspaceViewModel.Title))
			OnPropertyChanged(nameof(Title));
	}

	// A layout can arrive before the hierarchy says which root is ours so the latest one waits for it
	private ProceduralVoxelLayout? m_unmatchedLayout;

	private void OnHierarchyChanged() {
		if (m_unmatchedLayout is { } waiting && waiting.RootUid == Workspace.RootUid) {
			m_unmatchedLayout = null;
			OnLayout(waiting);
		}
	}

	private void OnLayout(ProceduralVoxelLayout e) {
		if (m_disposed) return;
		if (Workspace.RootUid is null || e.RootUid != Workspace.RootUid) {
			if (Workspace.RootUid is null && OwnsEngine) m_unmatchedLayout = e;
			return;
		}

		Pieces = e.Pieces.Select(p => new VoxelPieceInfo(
			p.Uid,
			p.Name,
			p.Kind,
			(p.Min.X, p.Min.Y, p.Min.Z),
			(p.Max.X, p.Max.Y, p.Max.Z),
			Color.FromRgb(ToByte(p.R), ToByte(p.G), ToByte(p.B)),
			Enumerable.Range(0, p.Planes.Count / 4)
				.Select(i => ((double)p.Planes[i * 4], (double)p.Planes[i * 4 + 1], (double)p.Planes[i * 4 + 2],
					(double)p.Planes[i * 4 + 3]))
				.ToArray(),
			p.Resizable,
			(int)p.ColorId)).ToArray();

		// A layout without projections keeps the last ones
		if (e.Projections.Count == 3)
			Projections = e.Projections.Select(p => {
				var depths = new short[p.Depths.Length / 2];
				Buffer.BlockCopy(p.Depths.ToByteArray(), 0, depths, 0, depths.Length * 2);
				return new VoxelProjectionInfo(p.MinH, p.MinV, (int)p.Width, (int)p.Height, p.Colors.ToByteArray(), depths,
					p.EdgesH.ToByteArray(), p.EdgesV.ToByteArray());
			}).ToArray();
		else if (FourUp)
			Projections = [];

		if (PaletteUid != e.PaletteUid) {
			PaletteUid = e.PaletteUid;
			OnPropertyChanged(nameof(PaletteUid));
		}
		LayoutChanged?.Invoke();
	}

	private static byte ToByte(float value) {
		return (byte)Math.Clamp((int)Math.Round(value * 255.0), 0, 255);
	}

	private void OnNodePicked(NodePicked e) {
		if (m_disposed || e.WorkspaceHandle != Workspace.EffectiveHandle) return;
		Select(string.IsNullOrEmpty(e.Node) ? null : e.Node);
	}

	private void OnSelectionChanged() {
		if (SelectedPiece is { Resizable: true, ColorId: > 0 } piece) {
			m_applyingSelection = true;
			PaletteId = piece.ColorId;
			m_applyingSelection = false;
		}
		SelectionChanged?.Invoke();
	}

	public void EnsureEngine() {
		if (!OwnsEngine && !m_disposed) TakeEngine();
	}

	public void TakeEngine() {
		if (m_disposed) return;
		ViewportFocus.DetachedOwner = Workspace.EffectiveHandle;
		Hierarchy.MakeCurrent();
		Events.Send(new SetActiveWorkspace { Handle = Workspace.EffectiveHandle });
		SendViewState();
		Events.Send(new RequestHierarchyUpdate());
	}

	public void Undo() {
		Workspace.History.Undo();
	}

	public void Redo() {
		Workspace.History.Redo();
	}

	[RelayCommand]
	private async Task Save() {
		await Workspace.Save();
		OnPropertyChanged(nameof(Title));
	}

	public async Task<bool> ConfirmCloseAsync() {
		if (!Workspace.IsModified) return true;
		switch (await App.Modals.ShowSaveChanges(Workspace.Title ?? "Procedural voxel")) {
			case Components.Modals.SaveChangesResult.Save:
				return await Workspace.Save();
			case Components.Modals.SaveChangesResult.DontSave:
				return true;
			default:
				return false;
		}
	}

	public void Select(string? uid) {
		Hierarchy.SelectedNode = uid is null ? null : Hierarchy.Find(uid);
	}

	public void BeginEdit(string subject) {
		if (m_transaction != 0) return;
		m_transaction = s_nextTransaction++;
		Workspace.History.SetTransactionOpen(true);
		Events.Send(new WorkspaceHistoryTransaction {
			WorkspaceHandle = Workspace.Handle,
			Transaction = m_transaction,
			Phase = WorkspaceHistoryTransaction.Types.Phase.Begin,
			Operation = HistoryOperation.HistoryChangeValue,
			Node = SelectedUid ?? "",
			Subject = subject
		});
	}

	public void CommitEdit() {
		if (m_transaction == 0) return;
		Events.Send(new WorkspaceHistoryTransaction {
			WorkspaceHandle = Workspace.Handle,
			Transaction = m_transaction,
			Phase = WorkspaceHistoryTransaction.Types.Phase.Commit
		});
		Workspace.History.SetTransactionOpen(false);
		m_transaction = 0;
	}

	public void SetBounds(string uid, (int X, int Y, int Z) min, (int X, int Y, int Z) max) {
		Events.Send(new VoxelSetPieceBounds { Target = uid, Min = Int3(min), Max = Int3(max) });
	}

	// Buildup, carve and paint add a VoxelVolume
	public void CreateVolume(VoxelTool tool, (int X, int Y, int Z) min, (int X, int Y, int Z) max) {
		var type = tool switch {
			VoxelTool.Carve => "toast::CarveVolume",
			VoxelTool.Paint => "toast::PaintVolume",
			_ => "toast::FillVolume"
		};
		Events.Send(new VoxelCreatePiece {
			Parent = CreationParent(), Type = type, Min = Int3(min), Max = Int3(max), Script = DefaultScript ?? "",
			Mode = tool == VoxelTool.Buildup ? DefaultFillMode : 0u
		});
	}

	[RelayCommand]
	private void AddMesh() {
		EnsureEngine();
		Events.Send(new WorkspaceCreateNode { Parent = CreationParent(), Type = "toast::VoxelMesh" });
	}

	[RelayCommand]
	private void AddGroup() {
		EnsureEngine();
		Events.Send(new WorkspaceCreateNode { Parent = CreationParent(), Type = "toast::VoxelGroup" });
	}

	// Extrude grows part of a face, the source and the new piece end up in one group
	public void Extrude(string sourceUid, (int X, int Y, int Z) min, (int X, int Y, int Z) max, bool inward) {
		Events.Send(new VoxelExtrude { Source = sourceUid, Min = Int3(min), Max = Int3(max), Inward = inward });
	}

	// keep 0 both halves, 1 the side the normal points to, 2 the other one
	public void Cut((double X, double Y, double Z, double W) plane, uint keep) {
		var targets = CutTargets();
		if (targets.Count == 0) return;
		var e = new VoxelSplitPieces {
			Nx = (float)plane.X, Ny = (float)plane.Y, Nz = (float)plane.Z, W = (float)plane.W, Keep = keep
		};
		e.Targets.AddRange(targets);
		Events.Send(e);
	}

	public void PreviewCut((double X, double Y, double Z, double W)? plane) {
		if (!OwnsEngine || Workspace.RootUid is null) return;
		var p = plane ?? (0, 0, 0, 0);
		Events.Send(new SetVoxelCutPreview {
			Root = Workspace.RootUid,
			Active = plane is not null,
			Nx = (float)p.X, Ny = (float)p.Y, Nz = (float)p.Z, W = (float)p.W
		});
	}

	public void RotateSelection(int axis, int turns) {
		if (SelectedPiece is not { } piece) return;
		var e = new VoxelRotatePieces { Axis = (uint)axis, Turns = turns };
		e.Targets.Add(piece.Uid);
		Events.Send(e);
	}

	// searchAxis and searchStep walk from voxel to the first solid one
	public void Bucket((int X, int Y, int Z) voxel, int searchAxis, int searchStep) {
		Events.Send(new VoxelBucketFill {
			Target = Workspace.RootUid ?? "",
			Voxel = Int3(voxel),
			Id = (uint)Math.Clamp(PaletteId, 1, 255),
			Search = true,
			SearchAxis = (uint)searchAxis,
			SearchStep = searchStep
		});
	}

	[RelayCommand]
	private async Task Bake() {
		var virtualPath = await App.Modals.ShowSaveFile($"{Workspace.Title}_baked", ".tvox");
		if (virtualPath is null) return;
		if (Workspace.RootUid is not { } root) return;
		VoxelEditorActions.CreateVoxelAsset(virtualPath);
		Events.Send(new VoxelBake { Target = root, Path = virtualPath, Replace = false });
	}

	private static bool CanBakeFromHierarchy(HierarchyElement node) {
		return !node.IsInsidePrefab && (IsVolume(node) || node.Type.EndsWith("VoxelGroup", StringComparison.Ordinal));
	}

	private async Task BakeFromHierarchy(HierarchyElement node) {
		EnsureEngine();
		var pieces = IsVolume(node)
			? [node]
			: Descendants(node).Where(IsPiece).ToList();
		if (pieces.Count == 0) {
			await App.Modals.ShowWarning("Bake Voxel", "This group contains no voxel pieces");
			return;
		}

		var virtualPath = await App.Modals.ShowSaveFile($"{node.Name}_baked", ".tvox");
		if (virtualPath is null) return;
		VoxelEditorActions.CreateVoxelAsset(virtualPath);
		var e = new VoxelCollapsePieces { Path = virtualPath };
		e.Targets.AddRange(pieces.Select(p => p.Uid));
		Events.Send(e);
	}

	private static IEnumerable<HierarchyElement> Descendants(HierarchyElement node) {
		foreach (var child in node.Children) {
			yield return child;
			foreach (var descendant in Descendants(child)) yield return descendant;
		}
	}

	private static bool IsVolume(HierarchyElement node) {
		return node.Type.EndsWith("FillVolume", StringComparison.Ordinal) ||
		       node.Type.EndsWith("CarveVolume", StringComparison.Ordinal) ||
		       node.Type.EndsWith("PaintVolume", StringComparison.Ordinal);
	}

	private static bool IsPiece(HierarchyElement node) {
		return IsVolume(node) || node.Type.EndsWith("VoxelMesh", StringComparison.Ordinal);
	}

	// Split and Slice only cut the selected shape
	private List<string> CutTargets() {
		return SelectedPiece is { } piece ? [piece.Uid] : [];
	}

	private string CreationParent() {
		if (Hierarchy.SelectedNode is { Type: "toast::VoxelGroup" } group) return group.Uid;
		return Workspace.RootUid ?? "";
	}

	private static VoxelInt3 Int3((int X, int Y, int Z) v) {
		return new VoxelInt3 { X = v.X, Y = v.Y, Z = v.Z };
	}

	[RelayCommand]
	private void SetTool(string tool) {
		ActiveTool = Enum.Parse<VoxelTool>(tool);
		OnPropertyChanged(nameof(ActiveTool));
	}

	[RelayCommand]
	private void SetViewMode(string mode) {
		ViewMode = Enum.Parse<VoxelViewMode>(mode);
		OnPropertyChanged(nameof(ViewMode));
	}

	partial void OnActiveToolChanged(VoxelTool value) {
		SendTool();
		if (value is not (VoxelTool.Split or VoxelTool.Slice)) PreviewCut(null);
	}

	partial void OnPaletteIdChanged(int value) {
		SendTool();
		// Picking a color recolors the selected volume too
		if (!m_applyingSelection && SelectedPiece is { Resizable: true } piece && piece.ColorId != value)
			Events.Send(new NodeChangeParam { Node = piece.Uid, Parameter = "id", Value = value.ToString() });
	}

	partial void OnDefaultScriptChanged(string? value) {
		SendTool();
	}

	partial void OnDefaultFillModeNameChanged(string value) {
		SendTool();
	}

	partial void OnViewModeChanged(VoxelViewMode value) {
		SendRenderMode();
	}

	partial void OnShowUnitGridChanged(bool value) {
		SendOverlays();
	}

	partial void OnShowVoxelGridChanged(bool value) {
		SendOverlays();
	}

	partial void OnShowVoxelEdgesChanged(bool value) {
		SendOverlays();
	}

	partial void OnShowEdgesChanged(bool value) {
		SendOverlays();
		LayoutChanged?.Invoke();
	}

	partial void OnFourUpChanged(bool value) {
		SendOverlays();
	}

	partial void OnShowOthersChanged(bool value) {
		if (OwnsEngine) SendShowOthers();
	}

	private void SendViewState() {
		SendTool();
		SendRenderMode();
		SendOverlays();
		SendShowOthers();
		Events.Send(new SetCoordinateSpace { World = false });
		Events.Send(new SetSnapping { Kind = 0, Enabled = true, Value = (float)VoxelSize });
		Events.Send(new SetSnapping { Kind = 1, Enabled = true, Value = (float)RightAngle });
		Events.Send(new SetSnapping { Kind = 2, Enabled = false, Value = (float)VoxelSize });
	}

	private void SendShowOthers() {
		Events.Send(new SetShowOthers {
			Show = ShowOthers && CanShowOthers,
			Workspace = Workspace.EffectiveHandle,
			SourceWorkspace = SourceWorkspace,
			SourceInstance = SourceInstance
		});
	}

	private void SendOverlays() {
		if (!OwnsEngine) return;
		Events.Send(new SetVoxelEditorOverlays {
			UnitGrid = ShowUnitGrid,
			VoxelGrid = ShowVoxelGrid,
			Edges = ShowEdges,
			VoxelEdges = ShowVoxelEdges,
			Projections = FourUp
		});
	}

	private void SendTool() {
		if (!OwnsEngine) return;
		Events.Send(new SetVoxelTool {
			Tool = (uint)ActiveTool, PaintId = (uint)Math.Clamp(PaletteId, 1, 255), DefaultScript = DefaultScript ?? "",
			DefaultMode = DefaultFillMode
		});
		var gizmo = ActiveTool == VoxelTool.Move ? GizmoTool.VolumeFaces : GizmoTool.Select;
		Events.Send(new SetGizmoTool { Tool = (uint)gizmo });
	}

	private void SendRenderMode() {
		if (!OwnsEngine) return;
		Events.Send(new SetRenderMode { Mode = (uint)ToRenderMode(ViewMode) });
	}

	private static RenderMode ToRenderMode(VoxelViewMode mode) {
		return mode switch {
			VoxelViewMode.Albedo => RenderMode.Albedo,
			VoxelViewMode.MetallicRoughness => RenderMode.MetallicRoughness,
			VoxelViewMode.PaletteId => RenderMode.VoxelPaletteId,
			VoxelViewMode.PhysicsMaterial => RenderMode.VoxelMaterials,
			VoxelViewMode.DdaSteps => RenderMode.VoxelSteps,
			_ => RenderMode.Lit
		};
	}

	public void Dispose() {
		if (m_disposed) return;
		CommitEdit();
		PreviewCut(null);
		if (OwnsEngine)
			Events.Send(new SetVoxelEditorOverlays { UnitGrid = false, VoxelGrid = false, Edges = false, VoxelEdges = false });
		m_disposed = true;
		Workspace.PropertyChanged -= OnWorkspaceChanged;
		m_listener.Dispose();
		Inspector.Dispose();
		Hierarchy.Dispose();
		if (ViewportFocus.DetachedOwner == Workspace.EffectiveHandle) ViewportFocus.DetachedOwner = 0;
		if (m_ownsWorkspace) {
			Events.Send(new SetFocusedNode { Node = "" });
			Events.Send(new WorkspaceDestroy { Handle = Workspace.Handle });
			Workspace.Dispose();
		}
		GC.SuppressFinalize(this);
	}
}
