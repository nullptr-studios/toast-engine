using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.Media;
using editor.LuaEditor.Editing;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Layout;
using editor.LuaEditor.Types;
using Size = Avalonia.Size;

namespace editor.LuaEditor.Canvas;

public sealed record SearchRequest(Vec World, Point Screen, OutputPin? From, InputPin? Into, InsertPoint? Insert, bool StatementsOnly);

public sealed record ConstantEditRequest(InputPin Pin, Avalonia.Rect Screen, string Text);

public sealed record GroupEditRequest(LayoutGroup Group, Avalonia.Rect Screen);

public enum DragMode { None, Pan, Nodes, Box, Wire, Exec, Group, Minimap, Region, Reroute, GroupResize, Proxy }

public sealed partial class GraphCanvas : Control {
	public static readonly StyledProperty<GraphSession?> SessionProperty = AvaloniaProperty.Register<GraphCanvas, GraphSession?>(nameof(Session));

	public bool ShowModuleSetup { get; set; }

	private DragMode m_drag;
	private Point m_pointerScreen;
	private Vec m_pointerWorld;
	private Point m_pressScreen;
	private Point m_boxStart;
	private bool m_moved;
	private Slot? m_hoverSlot;
	private Slot? m_hoverTarget;
	private InputPin? m_hoverWire;
	private Point? m_wireAnchor;
	private InputPin? m_selectedProxy;
	private GraphNode? m_docCandidate;
	private GraphNode? m_docNode;
	private Point m_docAt;
	private Avalonia.Threading.DispatcherTimer? m_docTimer;

	private void TrackDoc(GraphNode? overHeader) {
		if (overHeader == m_docCandidate) return;
		m_docCandidate = overHeader;
		if (m_docNode is not null) {
			m_docNode = null;
			InvalidateVisual();
		}
		m_docTimer?.Stop();
		if (overHeader is null) return;
		m_docTimer ??= new Avalonia.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(450) };
		m_docTimer.Tick -= OnDocTick;
		m_docTimer.Tick += OnDocTick;
		m_docTimer.Start();
	}

	private void OnDocTick(object? sender, EventArgs e) {
		m_docTimer?.Stop();
		if (m_docCandidate is null || m_drag != DragMode.None) return;
		m_docNode = m_docCandidate;
		m_docAt = m_pointerScreen;
		InvalidateVisual();
	}

	private Point? m_hoverRail;
	private CompatResult m_hoverCompat = CompatResult.Ok;
	private OutputPin? m_wireFromOutput;
	private InputPin? m_wireFromInput;
	private Slot? m_execFrom;
	private FrameReturnPort? m_wireFromReturn;
	private FunctionDecl? m_execFromFrameExit;
	private Statement? m_hoverExecTarget;
	private LayoutGroup? m_selectedGroup;
	private int m_resizeEdges;
	private Layout.Rect m_resizeStart;
	private Vec m_resizePress;
	private RegionInfo? m_dragRegion;
	private Vec m_lastNodeDragWorld;
	private List<GraphNode> m_dragNodes = [];
	private bool m_fittedOnce;
	private bool m_moveAllWires;
	private object? m_reroutePin;
	private int m_rerouteIndex;

	public GraphCanvas() {
		Focusable = true;
		ClipToBounds = true;
		PointerTouchPadGestureMagnify += OnTouchpadMagnify;
		DragDrop.SetAllowDrop(this, true);
		AddHandler(DragDrop.DragOverEvent, OnDragOverVariable);
		AddHandler(DragDrop.DropEvent, OnDropVariable);
	}

	public GraphSession? Session {
		get => GetValue(SessionProperty);
		set => SetValue(SessionProperty, value);
	}

	public double Zoom { get; private set; } = 1;
	public double PanX { get; private set; } = 40;
	public double PanY { get; private set; } = 60;
	public bool MinimapVisible { get; set; } = true;

	public event Action<SearchRequest>? SearchRequested;
	public event Action<ConstantEditRequest>? ConstantEditRequested;
	public event Action<GroupEditRequest>? GroupEditRequested;
	public event Action? SaveRequested;
	public event Action? FindRequested;
	public event Action<GraphNode, Point>? NodeMenuRequested;

	public event Action<Avalonia.Rect, string, Action<string>>? RenameRequested;
	public event Action<InputPin, Avalonia.Rect, IReadOnlyList<GraphSession.PinChoice>>? ChoiceMenuRequested;

	static GraphCanvas() {
		SessionProperty.Changed.AddClassHandler<GraphCanvas>((canvas, e) => canvas.OnSessionChanged(e.OldValue as GraphSession, e.NewValue as GraphSession));
		AffectsRender<GraphCanvas>(SessionProperty);
	}

	private void OnSessionChanged(GraphSession? old, GraphSession? current) {
		if (old is not null) old.Changed -= OnGraphChanged;
		if (current is not null) current.Changed += OnGraphChanged;
		m_fittedOnce = false;
		OnGraphChanged();
	}

	private void OnGraphChanged() {
		RefreshCaches();
		if (Session is not null && m_selectedGroup is not null && !Session.Layout.Groups.Contains(m_selectedGroup)) m_selectedGroup = null;
		InvalidateVisual();
	}

	protected override Size ArrangeOverride(Size finalSize) {
		var size = base.ArrangeOverride(finalSize);
		if (!m_fittedOnce && Session is not null && size.Width > 50 && size.Height > 50) {
			m_fittedOnce = true;
			FrameAll();
		}
		return size;
	}

	private Vec ToWorld(Point screen) => new((screen.X - PanX) / Zoom, (screen.Y - PanY) / Zoom);

	private Point ToScreen(Vec world) => new(world.X * Zoom + PanX, world.Y * Zoom + PanY);

	private void ZoomAt(Point screen, double factor) {
		var world = ToWorld(screen);
		Zoom = Math.Clamp(Zoom * factor, 0.1, 2.5);
		PanX = screen.X - world.X * Zoom;
		PanY = screen.Y - world.Y * Zoom;
		InvalidateVisual();
	}

	public void FrameAll() {
		var nodes = Session is null ? [] : VisibleNodes().ToList();
		if (nodes.Count == 0) return;
		Frame(nodes.Select(NodeRect).Aggregate((a, b) => a.Union(b)));
	}

	public void FrameNodes(IEnumerable<GraphNode> nodes) {
		var list = nodes.Where(n => Session!.Layout.IsPlaced(n) && !IsModuleSetupNode(n) && !Session.Layout.IsHidden(n)).Select(NodeRect).ToList();
		if (list.Count > 0) Frame(list.Aggregate((a, b) => a.Union(b)));
	}

	private void Frame(Avalonia.Rect world) {
		var area = Bounds.Size;
		if (area.Width < 10 || area.Height < 10) return;
		world = world.Inflate(60);
		Zoom = Math.Clamp(Math.Min(area.Width / world.Width, area.Height / world.Height), 0.1, 1.2);
		PanX = (area.Width - world.Width * Zoom) / 2 - world.X * Zoom;
		PanY = (area.Height - world.Height * Zoom) / 2 - world.Y * Zoom;
		InvalidateVisual();
	}

	protected override void OnPointerWheelChanged(PointerWheelEventArgs e) {
		base.OnPointerWheelChanged(e);
		var p = e.GetPosition(this);
		if (e.KeyModifiers.HasFlag(KeyModifiers.Control)) {
			ZoomAt(p, Math.Pow(1.0015, e.Delta.Y * 120));
		} else {
			PanX += e.Delta.X * 60;
			PanY += e.Delta.Y * 60;
			InvalidateVisual();
		}
		e.Handled = true;
	}

	private void OnTouchpadMagnify(object? sender, PointerDeltaEventArgs e) {
		var amount = e.Delta.X != 0 ? e.Delta.X : e.Delta.Y;
		ZoomAt(e.GetPosition(this), Math.Max(0.1, 1.0 + amount));
		e.Handled = true;
	}

	private GraphNode? NodeAt(Vec world) {
		if (Session is null) return null;
		GraphNode? hit = null;
		foreach (var node in VisibleNodes()) {
			var r = NodeRect(node);
			// nodes inside a frame must stay selectable
			if (world.X >= r.X && world.X <= r.Right && world.Y >= r.Y && world.Y <= r.Bottom && (hit is null || r.Width * r.Height <= NodeRect(hit).Width * NodeRect(hit).Height)) hit = node;
		}
		if (hit is not null && NodeShape.ExpansionOf(hit) is not null) {
			var r = NodeRect(hit);
			var (railLeft, railRight) = DefaultMeasure.Rails(hit);
			if (world.X > r.X + railLeft && world.X < r.Right - railRight && world.Y > r.Y + DefaultMeasure.HeaderHeight) return null;
		}
		return hit;
	}

	private Slot? SlotAt(Vec world) {
		if (Session is null) return null;
		Slot? best = null;
		var bestDistance = (PinRadius + 6) * (PinRadius + 6) / (Zoom < 1 ? Zoom : 1);
		foreach (var node in VisibleNodes()) {
			var r = NodeRect(node);
			if (world.X < r.X - 12 || world.X > r.Right + 12 || world.Y < r.Y || world.Y > r.Bottom) continue;
			foreach (var slot in SlotsOf(node)) {
				var p = SlotPoint(slot);
				var d = (p.X - world.X) * (p.X - world.X) + (p.Y - world.Y) * (p.Y - world.Y);
				if (d <= bestDistance) {
					best = slot;
					bestDistance = d;
				}
			}
		}
		return best;
	}

	private Slot? ConstantAt(Vec world) {
		if (Session is null) return null;
		foreach (var node in VisibleNodes()) {
			foreach (var slot in SlotsOf(node)) {
				if (slot.Kind != SlotKind.DataIn || slot.In is not { } candidate || (candidate.Source is not null && InlineRules.EnumValueOf(candidate) is null)) continue;
				var label = CanvasTheme.Default.Format(slot.Label, 11.5, CanvasTheme.Default.TextMuted);
				if (ConstantRect(slot, SlotPoint(slot), label.Width).Contains(new Point(world.X, world.Y))) return slot;
			}
		}
		return null;
	}

	private const int EdgeLeft = 1, EdgeTop = 2, EdgeRight = 4, EdgeBottom = 8;

	private (LayoutGroup? Group, int Edges) GroupEdgeAt(Vec world) {
		if (Session is null) return (null, 0);
		var m = 7 / Zoom;
		foreach (var g in VisibleGroups().Reverse()) {
			var r = g.Rect;
			if (world.X < r.X - m || world.X > r.Right + m || world.Y < r.Y - m || world.Y > r.Bottom + m) continue;
			var edges = (Math.Abs(world.X - r.X) <= m ? EdgeLeft : 0) | (Math.Abs(world.X - r.Right) <= m ? EdgeRight : 0)
				| (Math.Abs(world.Y - r.Y) <= m ? EdgeTop : 0) | (Math.Abs(world.Y - r.Bottom) <= m ? EdgeBottom : 0);
			if (edges != 0) return (g, edges);
		}
		return (null, 0);
	}

	private static Cursor ResizeCursor(int edges) => new(edges switch {
		EdgeLeft or EdgeRight => StandardCursorType.SizeWestEast,
		EdgeTop or EdgeBottom => StandardCursorType.SizeNorthSouth,
		EdgeLeft | EdgeTop or EdgeRight | EdgeBottom => StandardCursorType.TopLeftCorner,
		_ => StandardCursorType.TopRightCorner
	});

	private (OutputPin? Output, Point Point, Slot? Exec)? FrameDotAt(Vec world) {
		if (Session is null) return null;
		var radius = 8 / Math.Min(Zoom, 1);
		foreach (var node in VisibleNodes()) {
			if (NodeShape.ExpansionOf(node) is not { } frame) continue;
			var rect = NodeRect(node);
			if (world.X < rect.X || world.X > rect.Right || world.Y < rect.Y || world.Y > rect.Bottom) continue;
			var (railLeft, railRight) = DefaultMeasure.Rails(node);
			var slots = SlotsOf(node);
			var parameters = slots.Where(s => s.Kind == SlotKind.DataIn).ToList();
			for (var i = 0; i < parameters.Count && i < frame.Callee.Entry.Params.Count; i++) {
				var p = new Point(rect.X + railLeft, SlotPoint(parameters[i]).Y);
				if ((p.X - world.X) * (p.X - world.X) + (p.Y - world.Y) * (p.Y - world.Y) <= radius * radius) return (frame.Callee.Entry.Params[i].Output, p, null);
			}
			if (slots.FirstOrDefault(s => s.Kind == SlotKind.ExecIn) is { } enter && NodeShape.SlotsOf(frame.Callee).FirstOrDefault(s => s.Kind == SlotKind.ExecOut && s.Child is not null) is { } start) {
				var p = new Point(rect.X + railLeft, SlotPoint(enter).Y);
				if ((p.X - world.X) * (p.X - world.X) + (p.Y - world.Y) * (p.Y - world.Y) <= radius * radius) return (null, p, start);
			}
		}
		return null;
	}

	private sealed record FrameReturnPort(FunctionDecl Function, int Index, Point Point, InputPin? Input);

	private IEnumerable<FrameReturnPort> FrameReturns(GraphNode host, FunctionDecl function) {
		var slots = SlotsOf(host);
		var results = slots.Where(s => s.Kind == SlotKind.DataOut && !s.IsCallableReference).ToList();
		var rect = NodeRect(host);
		var x = rect.Right - DefaultMeasure.Rails(host).Right;
		var ret = NodeShape.TrailingReturn(function.Entry.Body);
		if (results.Count == 0) {
			var row = slots.Where(s => !s.IsLeft).Select(s => s.Row).DefaultIfEmpty(-1).Max() + 1;
			var y = rect.Y + Session!.Layout.Measure.PinY(host, row, true);
			yield return new FrameReturnPort(function, 0, new Point(x, y), ret?.Values.FirstOrDefault());
		}
		for (var i = 0; i < results.Count; i++)
			yield return new FrameReturnPort(function, i, new Point(x, SlotPoint(results[i]).Y), ret?.Values.ElementAtOrDefault(i));
	}

	private bool NearFrameRail(Vec world, Point point) {
		var radius = 8 / Math.Min(Zoom, 1);
		var dx = point.X - world.X;
		var dy = point.Y - world.Y;
		return dx * dx + dy * dy <= radius * radius;
	}

	private FrameReturnPort? FrameReturnAt(Vec world) => VisibleNodes()
		.SelectMany(host => NodeShape.ExpansionOf(host) is { } frame ? FrameReturns(host, frame.Callee) : [])
		.FirstOrDefault(port => NearFrameRail(world, port.Point));

	private Point FrameExecPoint(Slot slot) {
		var (left, right) = DefaultMeasure.Rails(slot.Node);
		var rect = NodeRect(slot.Node);
		return new Point(slot.IsLeft ? rect.X + left : rect.Right - right, SlotPoint(slot).Y);
	}

	private (FunctionDecl Function, Point Point)? FrameExitAt(Vec world) {
		foreach (var host in VisibleNodes()) {
			if (NodeShape.ExpansionOf(host) is not { } frame ||
				SlotsOf(host).FirstOrDefault(s => s.Kind == SlotKind.ExecOut && s.Child is null) is not { } exit) continue;
			var point = FrameExecPoint(exit);
			if (NearFrameRail(world, point)) return (frame.Callee, point);
		}
		return null;
	}

	private InsertPoint? ExecInsertAt(Vec world) {
		var hit = ExecWireAt(world);
		if (hit.To is FunctionDecl function) {
			var body = function.Entry.Body;
			return new InsertPoint(body, NodeShape.TrailingReturn(body) is { } ret ? body.IndexOf(ret) : body.Statements.Count);
		}
		if (hit.To is not { Parent: { } block } to) return null;
		return new InsertPoint(block, block.IndexOf(to));
	}

	private (TableField Field, Avalonia.Rect Label)? TableRowAt(TableNode table, Vec world) {
		var rect = NodeRect(table);
		foreach (var slot in SlotsOf(table).Where(s => s.Kind == SlotKind.DataIn && s.In is not null)) {
			var p = SlotPoint(slot);
			if (Math.Abs(world.Y - p.Y) > DefaultMeasure.RowHeight / 2 || world.X < rect.X || world.X > rect.X + rect.Width * 0.6) continue;
			var field = table.Fields.FirstOrDefault(f => f.Value == slot.In || f.Key == slot.In);
			if (field is null) continue;
			var label = CanvasTheme.Default.Format(slot.Label, 11.5, CanvasTheme.Default.TextMuted);
			return (field, new Avalonia.Rect(p.X + 10, p.Y - 10, label.Width + 16, 20));
		}
		return null;
	}

	private LayoutGroup? GroupHeaderAt(Vec world)

 => VisibleGroups().LastOrDefault(g => world.X >= g.Rect.X && world.X <= g.Rect.Right && world.Y >= g.Rect.Y && world.Y <= g.Rect.Y + 26);

	private RegionInfo? RegionHeaderAt(Vec world) {
		if (Session is null) return null;
		foreach (var region in CanvasRegions()) {
			if (Session.Layout.RegionBounds(region) is not { } b) continue;
			if (RegionHeaderRect(region, b).Contains(new Point(world.X, world.Y))) return region;
		}
		return null;
	}

	private Statement? StatementOf(GraphNode node) => node as Statement ?? node.OwnerStatement;

	private void OnDragOverVariable(object? sender, DragEventArgs e) {
		e.DragEffects = e.DataTransfer.Contains(VariableDrag.Format) ? DragDropEffects.Copy : DragDropEffects.None;
		e.Handled = true;
	}

	private void OnDropVariable(object? sender, DragEventArgs e) {
		if (Session is null || e.DataTransfer.TryGetValue(VariableDrag.Format) is not { } drag || drag.Session != Session) return;
		e.Handled = true;
		var shift = e.KeyModifiers.HasFlag(KeyModifiers.Shift);
		var set = shift || e.KeyModifiers.HasFlag(KeyModifiers.Alt);
		CreateHierarchyNode(drag.Row, set, shift, ToWorld(e.GetPosition(this)));
	}

	public void CreateHierarchyNode(VariableRow row, bool set) =>
		CreateHierarchyNode(row, set, false, ToWorld(new Point(Bounds.Width / 2, Bounds.Height / 2)));

	private void CreateHierarchyNode(VariableRow row, bool set, bool shift, Vec world) {
		if (Session is not { } session || !row.CanDrag || row.Variable is not { } variable || variable.Declaration.Graph != session.Graph) return;
		var entry = row.Kind == VariableRowKind.Script
			? new NodeEntry(NodeKind.GetSelf, "Script Table", "Variables", "self", Name: "self")
			: session.HierarchyEntry(variable, row.Binding, row.Native, set, shift && row.Kind == VariableRowKind.Method);
		var into = entry.Kind != NodeKind.SetVariable && SlotAt(world) is { Kind: SlotKind.DataIn, In: { } pin } ? pin : null;
		session.Create(entry, world, into: into, insert: ExecInsertAt(world));
	}

	protected override void OnPointerPressed(PointerPressedEventArgs e) {
		base.OnPointerPressed(e);
		Focus();
		TrackDoc(null);
		if (Session is null) return;
		var point = e.GetCurrentPoint(this);
		m_pointerScreen = m_pressScreen = point.Position;
		m_pointerWorld = ToWorld(point.Position);
		m_moved = false;
		var props = point.Properties;
		var ctrl = e.KeyModifiers.HasFlag(KeyModifiers.Control);
		var shift = e.KeyModifiers.HasFlag(KeyModifiers.Shift);
		var alt = e.KeyModifiers.HasFlag(KeyModifiers.Alt);

		if (props.IsRightButtonPressed && ProxyAt(m_pointerWorld) is { Source: { } tagSource } tagPin) {
			e.Pointer.Capture(this);
			m_wireFromOutput = tagSource;
			m_wireFromInput = null;
			m_moveAllWires = false;
			Session.Disconnect(tagPin);
			m_drag = DragMode.Wire;
			return;
		}
		if (props.IsRightButtonPressed && WireAt(m_pointerWorld) is var (cutWire, _) && cutWire?.Source is { } cutSource) {
			e.Pointer.Capture(this);
			m_wireFromOutput = cutSource;
			m_wireFromInput = null;
			m_moveAllWires = false;
			Session.Disconnect(cutWire);
			m_drag = DragMode.Wire;
			return;
		}
		if (props.IsMiddleButtonPressed || props.IsRightButtonPressed) {
			m_drag = DragMode.Pan;
			e.Pointer.Capture(this);
			return;
		}
		if (!props.IsLeftButtonPressed) return;
		e.Pointer.Capture(this);

		if (MinimapVisible && MinimapRect(Bounds.Size).Contains(point.Position)) {
			m_drag = DragMode.Minimap;
			MoveViewFromMinimap(point.Position);
			return;
		}

		var world = m_pointerWorld;
		m_selectedProxy = null;
		if (FrameReturnAt(world) is { } returned) {
			if (alt && returned.Input is { } input) Session.Disconnect(input);
			else {
				m_wireFromReturn = returned;
				m_wireFromInput = returned.Input;
				m_wireAnchor = returned.Point;
				m_drag = DragMode.Wire;
			}
			return;
		}
		if (FrameExitAt(world) is { } exit) {
			m_execFromFrameExit = exit.Function;
			m_wireAnchor = exit.Point;
			m_drag = DragMode.Exec;
			return;
		}
		if (FrameDotAt(world) is { } dot) {
			m_wireAnchor = dot.Point;
			if (dot.Exec is { } start) {
				m_execFrom = start;
				m_drag = DragMode.Exec;
				return;
			}
			m_wireFromOutput = dot.Output;
			m_wireFromInput = null;
			m_moveAllWires = false;
			m_drag = DragMode.Wire;
			return;
		}
		if (SlotAt(world) is { } slot) {
			BeginSlotDrag(slot, ctrl, alt);
			return;
		}
		if (ProxyAt(world) is { } tag) {
			if (alt) {
				Session.Disconnect(tag);
				m_drag = DragMode.None;
				return;
			}
			m_selectedProxy = tag;
			Session.ClearSelection();
			m_selectedGroup = null;
			m_drag = DragMode.Proxy;
			m_lastNodeDragWorld = world;
			InvalidateVisual();
			return;
		}
		if (alt && WireAt(world) is var (cutPin, _) && cutPin is not null) {
			Session.Disconnect(cutPin);
			m_drag = DragMode.None;
			return;
		}
		if (RerouteAt(world) is var (reroutePin, rerouteIndex) && reroutePin is not null) {
			if (e.ClickCount >= 2) {
				Session.RemoveReroute(reroutePin, rerouteIndex);
			} else {
				m_drag = DragMode.Reroute;
				m_reroutePin = reroutePin;
				m_rerouteIndex = rerouteIndex;
			}
			return;
		}
		if (e.ClickCount >= 2 && WireAt(world) is var (wirePin, segment) && wirePin is not null) {
			m_drag = DragMode.None;
			Session.AddReroute(wirePin, world, segment);
			return;
		}
		if (e.ClickCount >= 2 && ExecWireAt(world) is var (execTo, execSegment) && execTo is not null) {
			m_drag = DragMode.None;
			Session.AddReroute(execTo, world, execSegment);
			return;
		}
		if (ConstantAt(world) is { } constantSlot && e.ClickCount >= 1 && NodeAt(world) is not null) {
			EditConstant(constantSlot);
			m_drag = DragMode.None;
			e.Handled = true;
			return;
		}
		if (RegionHeaderAt(world) is { } region) {
			if (e.ClickCount >= 2 || ToggleButtonRect(RegionHeaderRect(region, Session.Layout.RegionBounds(region)!.Value)).Contains(new Point(world.X, world.Y))) {
				ToggleRegion(region);
				m_drag = DragMode.None;
			} else {
				m_drag = DragMode.Region;
				m_dragRegion = region;
				m_lastNodeDragWorld = world;
			}
			return;
		}
		if (NodeAt(world) is { } node) {
			var statement = node;
			if (e.ClickCount >= 2 && node is FunctionNode closure) {
				JumpToClosure(closure);
				m_drag = DragMode.None;
				return;
			}
				if (node is TableNode named && TableRowAt(named, world) is { } row && (row.Field.Kind == TableFieldKind.Keyed && row.Field.Key is { Source: null } || e.ClickCount >= 2 && row.Field.Kind == TableFieldKind.Named)) {
					var tableNode = named;
					var field = row.Field;
					var screenAt = ToScreen(new Vec(row.Label.X, row.Label.Y));
					var box = new Avalonia.Rect(screenAt.X, screenAt.Y, Math.Max(140, row.Label.Width * Zoom), Math.Max(20, row.Label.Height * Zoom));
					if (field.Kind == TableFieldKind.Named) RenameRequested?.Invoke(box, field.Name ?? "", text => Session?.RenameTableField(tableNode, field, text.Trim()));
					else if (field.Key is { } key) RenameRequested?.Invoke(box, NodeShape.KeyText(key), text => Session?.SetTableKey(tableNode, field, text));
					m_drag = DragMode.None;
					e.Handled = true;
					return;
				}
				if (e.ClickCount >= 2 && (node is MemberGet { Object.Source: not { Binding: { } } } || node is AssignStatement { Targets: [{ Kind: TargetKind.Member }] }) && world.Y <= NodeRect(node).Y + DefaultMeasure.HeaderHeight) {
					var access = node;
					var current = node is MemberGet member ? member.Name : ((AssignStatement)node).Targets[0].Name ?? "";
					var head = ToScreen(new Vec(NodeRect(node).X, NodeRect(node).Y));
					RenameRequested?.Invoke(new Avalonia.Rect(head.X, head.Y, Math.Max(160, NodeRect(node).Width * Zoom), 24), current, text => Session?.RenameAccess(access, text.Trim()));
					m_drag = DragMode.None;
					e.Handled = true;
					return;
				}
				if (node is TableNode growing && TableAddRect(NodeRect(node)).Contains(new Point(world.X, world.Y))) {

					Session.AddTableEntry(growing, TableGrowsBy(growing));
					m_drag = DragMode.None;
					e.Handled = true;
					return;
				}
				if (Session.CanOpen(node) && NodeRect(node) is var expandRect && ToggleButtonRect(expandRect).Contains(new Point(world.X, world.Y))) {
				Session.ToggleExpanded(node);
				m_drag = DragMode.None;
				return;
			}
			if (e.ClickCount >= 2 && JumpToCallee(node)) {
				m_drag = DragMode.None;
				return;
			}
			if (ctrl || shift) Session.Select([node], additive: true);
			else if (!Session.Selection.Contains(node)) Session.Select([node]);
			m_selectedGroup = null;
			m_drag = DragMode.Nodes;
			m_dragNodes = Session.Selection.ToList();
			m_lastNodeDragWorld = world;
			_ = statement;
			return;
		}
		if (GroupEdgeAt(world) is var (resizeGroup, resizeEdges) && resizeGroup is not null) {
			m_selectedGroup = resizeGroup;
			m_resizeEdges = resizeEdges;
			m_resizeStart = resizeGroup.Rect;
			m_resizePress = world;
			m_drag = DragMode.GroupResize;
			Session.ClearSelection();
			InvalidateVisual();
			return;
		}
		if (GroupHeaderAt(world) is { } group) {

			if (e.ClickCount >= 2) {
				GroupEditRequested?.Invoke(new GroupEditRequest(group, GroupScreenRect(group)));
				m_drag = DragMode.None;
				return;
			}
			m_selectedGroup = group;
			m_drag = DragMode.Group;
			m_lastNodeDragWorld = world;
			Session.ClearSelection();
			InvalidateVisual();
			return;
		}

		m_selectedGroup = null;
		m_drag = DragMode.Box;
		m_boxStart = point.Position;
		if (!ctrl && !shift) Session.ClearSelection();
	}

	private GraphNode? m_callableWireHost;

	private static GraphNode? CallableHost(Slot slot) => slot.IsCallableReference ? slot.Node : null;

	private void BeginSlotDrag(Slot slot, bool ctrl, bool alt) {
		var session = Session!;
		switch (slot.Kind) {
			case SlotKind.DataOut:
				m_drag = DragMode.Wire;
				m_wireFromOutput = slot.Out;
				m_wireFromInput = null;
				m_callableWireHost = CallableHost(slot);
				var isReference = m_callableWireHost is not null;
				if (isReference) m_wireAnchor = SlotPoint(slot);
				m_moveAllWires = ctrl && !isReference;
				break;
			case SlotKind.DataIn when slot.In is { } pin:
				if (alt) {
					session.Disconnect(pin);
					m_drag = DragMode.None;
				} else if (pin.Source is { } source && ctrl) {
					m_wireFromOutput = source;
					m_wireFromInput = null;
					session.Disconnect(pin);
					m_drag = DragMode.Wire;
				} else if (pin.Source is { } existing) {
					m_wireFromOutput = existing;
					m_wireFromInput = null;
					if (SlotFor(existing) is { } origin && session.Layout.IsHidden(origin.Node)) m_wireAnchor = FrameRailPoint(origin);
					session.Disconnect(pin);
					m_drag = DragMode.Wire;
				} else {
					m_wireFromInput = pin;
					m_wireFromOutput = null;
					m_drag = DragMode.Wire;
				}
				break;
			case SlotKind.ExecOut:
				m_drag = DragMode.Exec;
				m_execFrom = slot;
				break;
			default:
				m_drag = DragMode.None;
				break;
		}
	}

	protected override void OnPointerMoved(PointerEventArgs e) {
		base.OnPointerMoved(e);
		if (Session is null) return;
		var p = e.GetPosition(this);
		var delta = p - m_pointerScreen;
		m_pointerScreen = p;
		m_pointerWorld = ToWorld(p);
		if ((p - m_pressScreen).X * (p - m_pressScreen).X + (p - m_pressScreen).Y * (p - m_pressScreen).Y > 16) m_moved = true;
		var session = Session;

		switch (m_drag) {
			case DragMode.Pan:
				PanX += delta.X;
				PanY += delta.Y;
				break;
			case DragMode.Minimap:
				MoveViewFromMinimap(p);
				break;
			case DragMode.Nodes when m_moved:
				var step = m_pointerWorld - m_lastNodeDragWorld;
				session.Move(m_dragNodes, step);
				m_lastNodeDragWorld = m_pointerWorld;
				break;
			case DragMode.Region when m_moved && m_dragRegion is not null:
				session.MoveRegion(m_dragRegion, m_pointerWorld - m_lastNodeDragWorld);
				m_lastNodeDragWorld = m_pointerWorld;
				break;
			case DragMode.Group when m_moved && m_selectedGroup is not null:
				var d = m_pointerWorld - m_lastNodeDragWorld;
				session.Move(m_selectedGroup.Members, d);
				m_selectedGroup.Rect = m_selectedGroup.Rect.Offset(d);
				m_lastNodeDragWorld = m_pointerWorld;
				break;
			case DragMode.GroupResize when m_selectedGroup is not null:
				var gdx = m_pointerWorld.X - m_resizePress.X;
				var gdy = m_pointerWorld.Y - m_resizePress.Y;
				double left = m_resizeStart.X, top = m_resizeStart.Y, right = m_resizeStart.Right, bottom = m_resizeStart.Bottom;
				if ((m_resizeEdges & EdgeLeft) != 0) left = Math.Min(left + gdx, right - 80);
				if ((m_resizeEdges & EdgeRight) != 0) right = Math.Max(right + gdx, left + 80);
				if ((m_resizeEdges & EdgeTop) != 0) top = Math.Min(top + gdy, bottom - 60);
				if ((m_resizeEdges & EdgeBottom) != 0) bottom = Math.Max(bottom + gdy, top + 60);
				m_selectedGroup.Rect = new Layout.Rect(left, top, right - left, bottom - top);
				break;
			case DragMode.Proxy when m_moved && m_selectedProxy is not null:
				session.MoveProxy(m_selectedProxy, m_pointerWorld - m_lastNodeDragWorld);
				m_lastNodeDragWorld = m_pointerWorld;
				break;
			case DragMode.Reroute when m_reroutePin is not null:

				session.MoveReroute(m_reroutePin, m_rerouteIndex, m_pointerWorld);
				break;
			case DragMode.Wire:
				UpdateWireHover();
				break;
			case DragMode.Exec:
				m_hoverExecTarget = NodeAt(m_pointerWorld) is { } hit ? StatementOf(hit) : null;
				if (m_hoverExecTarget is not null && !NodeShape.HasExecutionIn(m_hoverExecTarget)) m_hoverExecTarget = null;
				break;
			case DragMode.None:
				var hover = SlotAt(m_pointerWorld);
				if (hover != m_hoverSlot) m_hoverSlot = hover;
				TrackDoc(hover is null && NodeAt(m_pointerWorld) is { } headerNode && m_pointerWorld.Y <= NodeRect(headerNode).Y + DefaultMeasure.HeaderHeight && NodeShape.ExpansionOf(headerNode) is null ? headerNode : null);
				m_hoverWire = hover is null ? WireAt(m_pointerWorld).Pin : null;
				var overToggle = VisibleNodes().Any(n => Session.CanOpen(n) && ToggleButtonRect(NodeRect(n)).Contains(new Point(m_pointerWorld.X, m_pointerWorld.Y)));
				var overEdge = hover is null && !overToggle && NodeAt(m_pointerWorld) is null ? GroupEdgeAt(m_pointerWorld).Edges : 0;
				Cursor = overEdge != 0 ? ResizeCursor(overEdge) : hover is not null || overToggle || FrameReturnAt(m_pointerWorld) is not null || FrameExitAt(m_pointerWorld) is not null ? new Cursor(StandardCursorType.Hand) : Cursor.Default;
				break;
		}
		InvalidateVisual();
	}

	private void UpdateWireHover() {
		m_hoverTarget = null;
		m_hoverCompat = CompatResult.Ok;
		m_hoverRail = null;
		if (m_wireFromOutput is { } railOutput && FrameReturnAt(m_pointerWorld) is { } returned) {
			m_hoverRail = returned.Point;
			if (returned.Input is { } input) m_hoverCompat = Session!.CanConnect(railOutput, input);
			return;
		}
		if ((m_wireFromReturn is not null || m_wireFromInput is not null) && FrameDotAt(m_pointerWorld) is { Output: not null } parameter) {
			m_hoverRail = parameter.Point;
			if (m_wireFromInput is { } input) m_hoverCompat = Session!.CanConnect(parameter.Output, input);
			return;
		}
		var slot = SlotAt(m_pointerWorld);
		if (slot is null) return;
		if (m_wireFromOutput is { } from && slot is { Kind: SlotKind.DataIn, In: { } pin }) {
			m_hoverTarget = slot;
			m_hoverCompat = Session!.CanConnect(from, pin);
		} else if (m_wireFromInput is { } into && slot is { Kind: SlotKind.DataOut, Out: { } output }) {
			m_hoverTarget = slot;
			m_hoverCompat = Session!.CanConnect(output, into);
		}
	}

	protected override void OnPointerReleased(PointerReleasedEventArgs e) {
		base.OnPointerReleased(e);
		var session = Session;
		var mode = m_drag;
		m_drag = DragMode.None;
		e.Pointer.Capture(null);
		if (session is null) return;
		var p = e.GetPosition(this);
		m_pointerWorld = ToWorld(p);

		switch (mode) {
			case DragMode.Pan when !m_moved:
				if (e.InitialPressMouseButton == MouseButton.Right)
					if (NodeAt(m_pointerWorld) is { } hitNode) NodeMenuRequested?.Invoke(hitNode, p);
					else SearchRequested?.Invoke(new SearchRequest(m_pointerWorld, p, null, null, ExecInsertAt(m_pointerWorld), false));
				break;
			case DragMode.Nodes when m_moved:
				session.FinishMove(m_dragNodes);
				break;
			case DragMode.Group when m_moved && m_selectedGroup is not null:
				session.FinishMove(m_selectedGroup.Members);
				break;
			case DragMode.Region when m_moved:
				session.FinishRegionMove();
				break;
			case DragMode.Box:
				FinishBoxSelect(e.KeyModifiers.HasFlag(KeyModifiers.Control) || e.KeyModifiers.HasFlag(KeyModifiers.Shift));
				break;
			case DragMode.Reroute when m_moved:
				session.CommitLayout();
				break;
			case DragMode.Proxy when m_moved:
				session.CommitLayout();
				break;
			case DragMode.GroupResize when m_selectedGroup is not null:
				if (m_moved) session.FinishResizeGroup(m_selectedGroup);
				break;
			case DragMode.Wire:
				FinishWire(p);
				break;
			case DragMode.Exec:
				FinishExec(p);
				break;
		}
		m_wireFromOutput = null;
		m_wireFromInput = null;
		m_wireFromReturn = null;
		m_execFromFrameExit = null;
		m_wireAnchor = null;
		m_callableWireHost = null;
		m_hoverTarget = null;
		m_hoverRail = null;
		m_execFrom = null;
		m_hoverExecTarget = null;
		m_dragRegion = null;
		InvalidateVisual();
	}

	private void FinishWire(Point screen) {
		var session = Session!;
		var world = ToWorld(screen);
		var slot = SlotAt(world);
		var rail = FrameDotAt(world);
		if (m_wireFromOutput is { } from) {
			if (FrameReturnAt(world) is { } returned) {
				session.ConnectFrameReturn(returned.Function, returned.Index, from, m_callableWireHost);
				return;
			}
			if (m_moveAllWires && slot is { Kind: SlotKind.DataOut, Out: { } target } && target != from) session.MoveWires(from, target);
			else if (slot is { Kind: SlotKind.DataIn, In: { } pin }) session.Connect(from, pin, m_callableWireHost);
			else if (slot is null) {
				if (NodeAt(world) is { } node && NodeAt(world) is not null && SessionPrimary(node) is { } primary) session.Connect(from, primary, m_callableWireHost);
				else if (m_moved) SearchRequested?.Invoke(new SearchRequest(world, screen, from, null, null, false));
			}
		} else if (m_wireFromReturn is { } returned) {
			var output = rail?.Output ?? (slot?.Kind == SlotKind.DataOut ? slot.Out : null);
			if (output is not null) session.ConnectFrameReturn(returned.Function, returned.Index, output, slot is null ? null : CallableHost(slot));
		} else if (m_wireFromInput is { } into) {
			if (rail?.Output is { } parameter) session.Connect(parameter, into);
			else if (slot is { Kind: SlotKind.DataOut, Out: { } output }) session.Connect(output, into, CallableHost(slot));
			else if (slot is null && m_moved) SearchRequested?.Invoke(new SearchRequest(world, screen, null, into, null, false));
		}
	}

	private static InputPin? SessionPrimary(GraphNode node) {
		if (node is Statement or ExprNode) return GraphSession.PrimaryInput(node);
		return null;
	}

	private void FinishExec(Point screen) {
		var session = Session!;
		var world = ToWorld(screen);
		if (m_execFromFrameExit is { } frame) {
			if (NodeAt(world) is ReturnStatement ret) session.ConnectFrameExit(frame, ret);
			return;
		}
		if (m_execFrom is null) return;
		if (FrameExitAt(world) is { } exit && m_execFrom.Node is Statement tail && m_execFrom.Child is null) {
			session.ConnectFrameExit(exit.Function, tail);
			return;
		}
		if (m_hoverExecTarget is { } target) {
			session.ConnectExec(m_execFrom, target);
		} else if (m_moved) {
			InsertPoint? point = null;
			if (m_execFrom.Child is { } child) point = new InsertPoint(child, 0);
			else if (m_execFrom.Node is Statement { Parent: { } block } source) point = new InsertPoint(block, block.IndexOf(source) + 1);
			SearchRequested?.Invoke(new SearchRequest(world, screen, null, null, point, true));
		}
	}

	private void FinishBoxSelect(bool additive) {
		var session = Session!;
		if (!m_moved) return;
		var a = ToWorld(m_boxStart);
		var b = ToWorld(m_pointerScreen);
		var box = new Avalonia.Rect(Math.Min(a.X, b.X), Math.Min(a.Y, b.Y), Math.Abs(a.X - b.X), Math.Abs(a.Y - b.Y));
		var hits = VisibleNodes().Where(n => NodeRect(n).Intersects(box)).ToList();
		if (!additive) session.ClearSelection();
		session.Select(hits, additive: true);
	}

	private void MoveViewFromMinimap(Point screen) {
		var map = MinimapRect(Bounds.Size);
		if (MinimapWorld(map) is not { } info) return;
		var (world, scale) = info;
		var wx = world.X + (screen.X - map.X - (map.Width - world.Width * scale) / 2) / scale;
		var wy = world.Y + (screen.Y - map.Y - (map.Height - world.Height * scale) / 2) / scale;
		PanX = Bounds.Width / 2 - wx * Zoom;
		PanY = Bounds.Height / 2 - wy * Zoom;
		InvalidateVisual();
	}

	private void ToggleRegion(RegionInfo region) {
		var layout = Session!.Layout;
		if (!layout.CollapsedRegions.Remove(region.Body)) layout.CollapsedRegions.Add(region.Body);
		Session.CommitLayout();
	}

	private bool JumpToCallee(GraphNode node) => NodeShape.CalleeOf(node) is { } function && ShowFunction(function);

	public bool ShowFunction(FunctionDecl function) {
		if (Session is not { Layout: { } layout }) return false;
		var region = layout.Keys.Regions.FirstOrDefault(r => r.Header == function);
		if (region is null) return false;
		layout.CollapsedRegions.Remove(region.Body);
		if (layout.Embedded.TryGetValue(region.Body, out var host)) {
			if (layout.Keys.RegionOf(host) is { } hostRegion) layout.CollapsedRegions.Remove(hostRegion.Body);
			FrameNodes([host]);
		} else {
			FrameNodes(layout.NodesOf(region));
		}
		return true;
	}

	private void JumpToClosure(FunctionNode closure) {
		var layout = Session!.Layout;
		var region = layout.Keys.Regions.FirstOrDefault(r => r.Header == closure);
		if (region is null) return;
		layout.CollapsedRegions.Remove(region.Body);
		FrameNodes(layout.NodesOf(region));
	}

	private void EditConstant(Slot slot) {
		var pin = slot.In!;
		if (GraphSession.IsBoolean(pin) && pin.Source is null) {
			Session!.Toggle(pin);
			return;
		}
		var label = CanvasTheme.Default.Format(slot.Label, 11.5, CanvasTheme.Default.TextMuted);
		var world = ConstantRect(slot, SlotPoint(slot), label.Width);
		var topLeft = ToScreen(new Vec(world.X, world.Y));
		var screen = new Avalonia.Rect(topLeft.X, topLeft.Y, world.Width * Zoom, world.Height * Zoom);
		if (Session!.ChoicesFor(pin) is { } choices) {
			ChoiceMenuRequested?.Invoke(pin, screen, choices);
			return;
		}
		ConstantEditRequested?.Invoke(new ConstantEditRequest(pin, screen, pin.Constant is { } current ? GraphSession.DisplayText(current) : ""));
	}

	private Avalonia.Rect GroupScreenRect(LayoutGroup group) {
		var tl = ToScreen(new Vec(group.Rect.X, group.Rect.Y));
		return new Avalonia.Rect(tl.X, tl.Y, group.Rect.W * Zoom, 26 * Zoom);
	}

	protected override void OnKeyDown(KeyEventArgs e) {
		base.OnKeyDown(e);
		if (Session is null || e.Handled) return;
		var key = e.Key.ToString();
		var ctrl = e.KeyModifiers.HasFlag(KeyModifiers.Control);
		var shift = e.KeyModifiers.HasFlag(KeyModifiers.Shift);
		var alt = e.KeyModifiers.HasFlag(KeyModifiers.Alt);
		if (e.Key == Key.Escape) {
			m_drag = DragMode.None;
			Session.ClearSelection();
			e.Handled = true;
			return;
		}
		if (Shortcuts.Find(key, ctrl, shift, alt) is not { } action) return;
		e.Handled = true;
		Execute(action);
	}

	public void Execute(CanvasAction action) {
		var session = Session;
		if (session is null) return;
		var at = m_pointerWorld;
		switch (action) {
			case CanvasAction.Branch: Make(NodeCatalog.Of(NodeKind.If), at); break;
			case CanvasAction.Sequence:
				if (session.Selection.OfType<Statement>().Count() >= 2) session.GroupSelectionAsSequence();
				else Make(NodeCatalog.Of(NodeKind.Sequence), at);
				break;
			case CanvasAction.ForEach: Make(NodeCatalog.Of(NodeKind.GenericFor), at); break;
			case CanvasAction.MakeTable: Make(NodeCatalog.Of(NodeKind.MakeTable), at); break;
			case CanvasAction.Add: Make(NodeCatalog.Operator(BinaryKind.Add), at); break;
			case CanvasAction.Subtract: Make(NodeCatalog.Operator(BinaryKind.Sub), at); break;
			case CanvasAction.Multiply: Make(NodeCatalog.Operator(BinaryKind.Mul), at); break;
			case CanvasAction.Divide: Make(NodeCatalog.Operator(BinaryKind.Div), at); break;
			case CanvasAction.Greater: Make(NodeCatalog.Operator(BinaryKind.Gt), at); break;
			case CanvasAction.Less: Make(NodeCatalog.Operator(BinaryKind.Lt), at); break;
			case CanvasAction.Equal: Make(NodeCatalog.Operator(BinaryKind.Eq), at); break;
			case CanvasAction.Not: Make(NodeCatalog.Operator(UnaryKind.Not), at); break;
			case CanvasAction.Number: Make(NodeCatalog.Of(NodeKind.Number), at); break;
			case CanvasAction.Vec2: Make(NodeCatalog.Vector("vec2"), at); break;
			case CanvasAction.Vec3: Make(NodeCatalog.Vector("vec3"), at); break;
			case CanvasAction.Quat: Make(NodeCatalog.Vector("quat"), at); break;
			case CanvasAction.Search:
				SearchRequested?.Invoke(new SearchRequest(at, ToScreen(at), m_wireFromOutput, m_wireFromInput, m_wireFromOutput is null && m_wireFromInput is null ? ExecInsertAt(at) : null, false));
				break;
			case CanvasAction.CommentBox:
				if (session.AddCommentBox() is { } group) {
					m_selectedGroup = group;
					GroupEditRequested?.Invoke(new GroupEditRequest(group, GroupScreenRect(group)));
				}
				break;
			case CanvasAction.GroupAsFunction: session.GroupSelectionAsFunction(); break;
			case CanvasAction.Straighten: StraightenSelection(); break;
			case CanvasAction.AlignTop: session.Align(AlignEdge.Top); break;
			case CanvasAction.AlignLeft: session.Align(AlignEdge.Left); break;
			case CanvasAction.AlignBottom: session.Align(AlignEdge.Bottom); break;
			case CanvasAction.AlignRight: session.Align(AlignEdge.Right); break;
			case CanvasAction.Tidy: session.Tidy(null, includePinned: false); break;
			case CanvasAction.Duplicate: session.Duplicate(); break;
			case CanvasAction.Delete:
				if (m_selectedProxy is { } tagToRemove) {
					session.Disconnect(tagToRemove);
					m_selectedProxy = null;
				} else if (m_selectedGroup is not null) {
					session.RemoveCommentBox(m_selectedGroup);
					m_selectedGroup = null;
				} else {
					session.Delete(session.Selection.ToList());
				}
				break;
			case CanvasAction.Copy: _ = CopyAsync(); break;
			case CanvasAction.Cut: _ = CutAsync(); break;
			case CanvasAction.Paste: _ = PasteAsync(at); break;
			case CanvasAction.SelectAll: session.Select(VisibleNodes()); break;
			case CanvasAction.Undo: session.Undo(); break;
			case CanvasAction.Redo: session.Redo(); break;
			case CanvasAction.Save: SaveRequested?.Invoke(); break;
			case CanvasAction.Find: FindRequested?.Invoke(); break;
			case CanvasAction.Home:
				if (session.Selection.Count > 0) FrameNodes(session.Selection);
				else FrameAll();
				break;
		}
		InvalidateVisual();
	}

	private OutputPin? SelectedOutput() => Session!.Selection.Count == 1 ? Session.Selection[0].Outputs.FirstOrDefault() : null;

	private void Make(NodeEntry entry, Vec at) {
		var session = Session!;
		var from = m_wireFromOutput;
		if (from is not null && from.Owner is Statement { Parent: null }) from = null;
		session.Create(entry, at, from, m_wireFromInput, ExecInsertAt(at));
		m_drag = DragMode.None;
		m_wireFromOutput = null;
		m_wireFromInput = null;
	}

	private void StraightenSelection() {
		var session = Session!;
		var selected = session.Selection.ToHashSet();
		var links = new List<(Slot From, Slot To)>();
		ForEachExecLink(null, (from, target) => {
			if (selected.Contains(from.Node) && selected.Contains(target) && SlotsOf(target).FirstOrDefault(s => s.Kind == SlotKind.ExecIn) is { } into)
				links.Add((from, into));
		});
		foreach (var node in selected)
			foreach (var into in SlotsOf(node))
				if (into.In is { Source: { } source } pin && (session.Layout.CallableSlot(pin) ?? SlotFor(source)) is { } from && selected.Contains(from.Node))
					links.Add((from, into));
		session.StraightenSelection(links);
	}

	private async System.Threading.Tasks.Task CopyAsync() {
		var text = Session!.CopyText();
		if (text.Length > 0 && TopLevel.GetTopLevel(this)?.Clipboard is { } clipboard) await clipboard.SetTextAsync(text);
	}

	private async System.Threading.Tasks.Task CutAsync() {
		await CopyAsync();
		Session!.Delete(Session.Selection.OfType<Statement>().ToList());
	}

	private async System.Threading.Tasks.Task PasteAsync(Vec at) {
		if (TopLevel.GetTopLevel(this)?.Clipboard is not { } clipboard) return;
		var text = await clipboard.TryGetTextAsync();
		if (!string.IsNullOrWhiteSpace(text)) Session!.Paste(text, at);
	}
}
