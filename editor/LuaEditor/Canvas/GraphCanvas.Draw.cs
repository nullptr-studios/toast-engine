using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Media;
using editor.LuaEditor.Editing;
using editor.LuaEditor.Graph;
using editor.LuaEditor.Layout;
using editor.LuaEditor.Types;
using Size = Avalonia.Size;

namespace editor.LuaEditor.Canvas;

public sealed partial class GraphCanvas {
	private const double PinRadius = 5;
	private const int ProxyThreshold = 3;
	private const double ConstantBoxWidth = DefaultMeasure.ConstantBoxWidth;

	private readonly Dictionary<GraphNode, IReadOnlyList<Slot>> m_slotCache = [];
	private readonly Dictionary<GraphNode, DiagnosticSeverity> m_nodeSeverity = [];
	private readonly Dictionary<InputPin, string> m_pinProblems = [];

	private IReadOnlyList<Slot> SlotsOf(GraphNode node) {
		if (!m_slotCache.TryGetValue(node, out var slots)) {
			slots = NodeShape.SlotsOf(node);
			if (node is ReturnStatement { IsFloating: true } && Session?.Layout is { } layout &&
				layout.HostBody(node) is { } body && layout.Embedded.ContainsKey(body))
				slots = [.. slots, new Slot(SlotKind.ExecOut, 0, "", node)];
			m_slotCache[node] = slots;
		}
		return slots;
	}

	private void RefreshCaches() {
		m_slotCache.Clear();
		m_nodeSeverity.Clear();
		m_pinProblems.Clear();
		if (Session is null) return;
		foreach (var d in Session.Graph.Diagnostics) {
			var node = InlineRules.Drawn(d.Node);
			if (!m_nodeSeverity.TryGetValue(node, out var current) || d.Severity > current) m_nodeSeverity[node] = d.Severity;
			if (d.Pin is not null) m_pinProblems[d.Pin] = d.Message;
		}
	}

	private IEnumerable<RegionInfo> CanvasRegions() => Session is { } session
		? session.Layout.Keys.Regions.Where(region => ShowModuleSetup || !region.IsModule) : [];

	private bool IsModuleSetupNode(GraphNode node) => !ShowModuleSetup && Session is { } session &&
		!node.IsExpanded && !session.Layout.Loose.Contains(node) && session.Layout.Keys.RegionOf(node) is { IsModule: true };

	private IEnumerable<LayoutGroup> VisibleGroups() => Session is { } session
		? session.Layout.Groups.Where(group => ShowModuleSetup || group.Members.Any(node => !IsModuleSetupNode(node))) : [];

	private IEnumerable<GraphNode> VisibleNodes(RegionInfo? only = null) {
		if (Session is null) yield break;
		var layout = Session.Layout;
		foreach (var region in CanvasRegions()) {
			if (only is not null && region != only) continue;
			if (only is null && layout.CollapsedRegions.Contains(region.Body)) continue;
			foreach (var node in layout.NodesOf(region)) {
				if (InlineRules.IsMerged(node) || !layout.IsPlaced(node) || layout.IsHidden(node)) continue;
				yield return node;
			}
		}
		if (only is not null) yield break;
		if (!ShowModuleSetup)
			foreach (var host in layout.Embedded.Values.Where(host => !layout.Loose.Contains(host) && layout.Keys.RegionOf(host) is { IsModule: true }))
				yield return host;
		foreach (var loose in layout.Loose) yield return loose;
	}

	private static StreamGeometry RoutedWire(Point from, Point to, GraphNode? fromNode, GraphNode? toNode) => Bezier(from, to);

	private Avalonia.Rect NodeRect(GraphNode node) {

		var b = Session!.Layout.Bounds(node);
		return new Avalonia.Rect(b.X, b.Y, b.W, b.H);
	}

	private Point SlotPoint(Slot slot) {
		var layout = Session!.Layout;
		var p = layout.Position(slot.Node);
		var size = layout.Measure.Measure(slot.Node);
		return new Point(slot.IsLeft ? p.X : p.X + size.W, p.Y + layout.Measure.PinY(slot.Node, slot.Row, !slot.IsLeft));
	}

	private Slot? SlotFor(InputPin pin) {
		var drawn = InlineRules.Drawn(pin.Owner);
		return SlotsOf(drawn).FirstOrDefault(s => s.In == pin);
	}

	private Slot? SlotFor(OutputPin pin) {
		var drawn = InlineRules.Drawn(pin.Owner);
		return SlotsOf(drawn).FirstOrDefault(s => s.Out == pin);
	}

	public override void Render(DrawingContext context) {
		base.Render(context);
		var theme = CanvasTheme.Default;
		var size = Bounds.Size;
		context.DrawRectangle(theme.Background, null, new Avalonia.Rect(size));
		using var clip = context.PushClip(new Avalonia.Rect(size));
		DrawGrid(context, theme, size);
		if (Session is null) return;

		using (context.PushTransform(Matrix.CreateScale(Zoom, Zoom) * Matrix.CreateTranslation(PanX, PanY))) {
			var view = new Avalonia.Rect(-PanX / Zoom, -PanY / Zoom, size.Width / Zoom, size.Height / Zoom);
			DrawGroups(context, theme, view);
			DrawRegions(context, theme, view);
			DrawFrames(context, theme, view);
			DrawWires(context, theme, view);
			DrawNodes(context, theme, view);
			DrawDragPreview(context, theme);
		}
		DrawBoxSelect(context, theme);
		DrawMinimap(context, theme, size);
		DrawDocCard(context, theme, size);
	}

	private void DrawDocCard(DrawingContext context, CanvasTheme theme, Size size) {
		if (m_docNode is not { } node || Session is null) return;
		var doc = NodeDoc.Of(node, Session.Catalog);
		var keyword = new SolidColorBrush(Color.Parse("#ff77a8"));
		var lines = new List<FormattedText> { theme.Format(doc.Title, 14, doc.Title.Split(' ', 2)[0] is "if" or "for" or "while" or "repeat" or "do" or "return" or "break" or "local" or "goto" or "not" ? keyword : theme.Text, true) };
		if (doc.Summary.Length > 0) lines.Add(theme.Format(doc.Summary, 12, theme.TextMuted));
		FormattedText Pin(DocPin pin) {
			var text = theme.Format("  " + pin.Name + ": " + pin.Type, 12, theme.Text);
			text.SetForegroundBrush(theme.TextMuted, 2 + pin.Name.Length, 1);
			text.SetForegroundBrush(new SolidColorBrush(Color.Parse(pin.Color)), 4 + pin.Name.Length, pin.Type.Length);
			return text;
		}
		if (doc.Inputs.Count > 0) {
			lines.Add(theme.Format("Inputs", 11, theme.TextMuted, true));
			lines.AddRange(doc.Inputs.Select(Pin));
		}
		if (doc.Outputs.Count > 0) {
			lines.Add(theme.Format("Outputs", 11, theme.TextMuted, true));
			lines.AddRange(doc.Outputs.Select(Pin));
		}

		foreach (var line in lines) line.MaxTextWidth = 360;
		var width = lines.Max(l => l.Width) + 20;
		var height = lines.Sum(l => l.Height + 3) + 14;
		var x = Math.Min(m_docAt.X + 14, Math.Max(4, size.Width - width - 4));
		var y = Math.Min(m_docAt.Y + 18, Math.Max(4, size.Height - height - 4));
		context.DrawRectangle(theme.NodeBody, new Pen(theme.TextMuted, 1), new Avalonia.Rect(x, y, width, height), 6, 6);
		var top = y + 8;
		foreach (var line in lines) {
			context.DrawText(line, new Point(x + 10, top));
			top += line.Height + 3;
		}
	}

	private void DrawGrid(DrawingContext context, CanvasTheme theme, Size size) {
		var step = 32 * Zoom;
		if (step < 7) return;
		var minor = new Pen(theme.GridMinor, 1);
		var major = new Pen(theme.GridMajor, 1);
		var startX = PanX % step;
		var startY = PanY % step;
		for (var x = startX; x < size.Width; x += step) {
			var index = (int)Math.Round((x - PanX) / step);
			context.DrawLine(index % 5 == 0 ? major : minor, new Point(x, 0), new Point(x, size.Height));
		}
		for (var y = startY; y < size.Height; y += step) {
			var index = (int)Math.Round((y - PanY) / step);
			context.DrawLine(index % 5 == 0 ? major : minor, new Point(0, y), new Point(size.Width, y));
		}
	}

	private void DrawGroups(DrawingContext context, CanvasTheme theme, Avalonia.Rect view) {
		foreach (var group in VisibleGroups()) {
			var r = new Avalonia.Rect(group.Rect.X, group.Rect.Y, group.Rect.W, group.Rect.H);
			if (!r.Intersects(view)) continue;
			var color = CanvasTheme.ParseColor(group.Color);
			var selected = ReferenceEquals(m_selectedGroup, group);
			context.DrawRectangle(new SolidColorBrush(color, 0.14), new Pen(new SolidColorBrush(color, selected ? 1 : 0.55), selected ? 2 : 1), r, 6, 6);
			context.DrawRectangle(new SolidColorBrush(color, 0.35), null, new Avalonia.Rect(r.X, r.Y, r.Width, 26), 6, 6);
			context.DrawText(theme.Format(group.Title, 13, theme.Text, true), new Point(r.X + 10, r.Y + 5));
		}
	}

	private void DrawRegions(DrawingContext context, CanvasTheme theme, Avalonia.Rect view) {
		var layout = Session!.Layout;
		foreach (var region in CanvasRegions()) {
			if (layout.Embedded.ContainsKey(region.Body) || layout.RegionBounds(region) is not { } b) continue;
			var collapsed = layout.CollapsedRegions.Contains(region.Body);
			var name = region.IsModule ? "Script" : region.Header is FunctionDecl d ? d.Name : region.Header is { } closure ? NodeShape.TitleOf(closure) : "function";
			var header = RegionHeaderRect(region, b);
			if (!header.Intersects(view) && !(b.X < view.Right && b.Y < view.Bottom && b.X + b.W > view.X && b.Y + b.H > view.Y)) continue;
			if (!collapsed) {
				var frame = RegionFrame(region, b);
context.DrawRectangle(new SolidColorBrush(Color.Parse("#ffffff"), 0.025), new Pen(new SolidColorBrush(Color.Parse("#ffffff"), 0.07), 1, dashStyle: DashStyle.Dash), frame, 8, 8);
			}
			var pinned = layout.PinnedRegions.Contains(region.Body);
			context.DrawRectangle(new SolidColorBrush(Color.Parse("#ffffff"), 0.09), null, header, 6, 6);
			DrawToggleButton(context, theme, header, !collapsed, theme.Text);
			context.DrawText(theme.Format(name + (pinned ? "  •" : ""), 14, theme.Text, true), new Point(header.X + 36, header.Y + 4));
		}
	}

	private Avalonia.Rect RegionFrame(RegionInfo region, Layout.Rect bounds) {
		var frame = new Avalonia.Rect(bounds.X - 12, bounds.Y - 30, bounds.W + 24, bounds.H + 42);
		var layout = Session!.Layout;
		foreach (var group in layout.Groups) {
			if (group.Members.FirstOrDefault(m => layout.Keys.KeyOf(m) is not null) is not { } member || layout.Keys.RegionOf(member) != region) continue;
			frame = frame.Union(new Avalonia.Rect(group.Rect.X - 12, group.Rect.Y - 30, group.Rect.W + 24, group.Rect.H + 42));
		}
		return frame;
	}

	private Avalonia.Rect RegionHeaderRect(RegionInfo region, Layout.Rect bounds) {
		var name = region.IsModule ? "Script" : region.Header is FunctionDecl d ? d.Name : region.Header is { } closure ? NodeShape.TitleOf(closure) : "function";
		var frame = RegionFrame(region, bounds);
		return new Avalonia.Rect(frame.X + 4, frame.Y + 4, Math.Max(140, name.Length * 9 + 60), 26);
	}

	private List<Point> RerouteOf(InputPin pin) {
		var layout = Session!.Layout;
		if (!layout.Reroutes.TryGetValue(pin, out var offsets)) return [];
		var origin = layout.Position(InlineRules.Drawn(pin.Owner));
		return offsets.Select(o => new Point(origin.X + o.X, origin.Y + o.Y)).ToList();
	}

	private static double Reach(Point a, Point b) => Math.Max(40, Math.Min(260, Math.Abs(b.X - a.X) * 0.55 + Math.Abs(b.Y - a.Y) * 0.1));

	private List<Point> ExecThrough(Statement to) {
		var layout = Session!.Layout;
		if (!layout.ExecReroutes.TryGetValue(to, out var offsets)) return [];
		var origin = layout.Position(to);
		return offsets.Select(o => new Point(origin.X + o.X, origin.Y + o.Y)).ToList();
	}

	private static StreamGeometry WireGeometry(Point from, IReadOnlyList<Point> through, Point to) {
		var geometry = new StreamGeometry();
		using var g = geometry.Open();
		var chain = new List<Point> { from };
		chain.AddRange(through);
		chain.Add(to);
		g.BeginFigure(from, false);
		for (var i = 0; i < chain.Count - 1; i++) {
			var a = chain[i];
			var b = chain[i + 1];
			var reach = Reach(a, b);
			g.CubicBezierTo(new Point(a.X + reach, a.Y), new Point(b.X - reach, b.Y), b);
		}
		g.EndFigure(false);
		return geometry;
	}

	private static Point OnBezier(Point a, Point b, double t) {
		var reach = Reach(a, b);
		var c1 = new Point(a.X + reach, a.Y);
		var c2 = new Point(b.X - reach, b.Y);
		var u = 1 - t;
		return new Point(u * u * u * a.X + 3 * u * u * t * c1.X + 3 * u * t * t * c2.X + t * t * t * b.X, u * u * u * a.Y + 3 * u * u * t * c1.Y + 3 * u * t * t * c2.Y + t * t * t * b.Y);
	}

	private (object? Wire, int Index) RerouteAt(Vec world) {
		if (Session is null) return (null, -1);
		var radius = 8 / Zoom;
		bool Near(Point p) => (p.X - world.X) * (p.X - world.X) + (p.Y - world.Y) * (p.Y - world.Y) <= radius * radius;
		foreach (var (pin, _) in Session.Layout.Reroutes) {
			if (IsModuleSetupNode(InlineRules.Drawn(pin.Owner))) continue;
			var points = RerouteOf(pin);
			for (var i = 0; i < points.Count; i++)
				if (Near(points[i])) return (pin, i);
		}
		foreach (var (statement, _) in Session.Layout.ExecReroutes) {
			if (IsModuleSetupNode(statement)) continue;
			var points = ExecThrough(statement);
			for (var i = 0; i < points.Count; i++)
				if (Near(points[i])) return (statement, i);
		}
		return (null, -1);
	}

	private int WireSegmentAt(Vec world, Point start, IReadOnlyList<Point> bends, Point end, double reach) {
		var chain = new List<Point> { start };
		chain.AddRange(bends);
		chain.Add(end);
		for (var i = 0; i < chain.Count - 1; i++) {
			var previous = chain[i];
			for (var step = 1; step <= 32; step++) {
				var point = OnBezier(chain[i], chain[i + 1], step / 32.0);
				var edge = point - previous;
				var length = edge.X * edge.X + edge.Y * edge.Y;
				var along = length == 0 ? 0 : Math.Clamp(((world.X - previous.X) * edge.X + (world.Y - previous.Y) * edge.Y) / length, 0, 1);
				var dx = previous.X + along * edge.X - world.X;
				var dy = previous.Y + along * edge.Y - world.Y;
				if (dx * dx + dy * dy <= reach * reach) return i;
				previous = point;
			}
		}
		return -1;
	}

	private Point? FrameReturnSource(InputPin pin) {
		if (pin.Source is not { } source || (Session!.Layout.CallableSlot(pin) ?? SlotFor(source)) is not { } from) return null;
		return IsModuleSetupNode(from.Node) ? null : Session.Layout.IsHidden(from.Node) ? FrameRailPoint(from) : SlotPoint(from);
	}

	private (InputPin? Pin, int Segment) WireAt(Vec world) {
		if (Session is null) return (null, -1);
		var layout = Session.Layout;
		var reach = 6 / Zoom;
		foreach (var node in VisibleNodes()) {
			if (NodeShape.ExpansionOf(node) is { } frame)
				foreach (var port in FrameReturns(node, frame.Callee))
					if (port.Input is { } input && FrameReturnSource(input) is { } source &&
						WireSegmentAt(world, source, RerouteOf(input), port.Point, reach) is >= 0 and var segment)
						return (input, segment);
			foreach (var slot in SlotsOf(node)) {
				if (slot.In is not { Source: { } source } pin || (layout.CallableSlot(pin) ?? SlotFor(source)) is not { } from || from.Node == node) continue;
				if (IsModuleSetupNode(from.Node)) continue;
				Point? start;
				if (layout.IsHidden(from.Node)) start = FrameRailPoint(from);
				else {
					if (layout.CallableSlot(pin) is null && source.Binding is not null &&
						(source.Consumers.Count >= ProxyThreshold || layout.Keys.RegionOf(from.Node) != layout.Keys.RegionOf(node))) continue;
					start = SlotPoint(from);
				}
				if (start is { } point && WireSegmentAt(world, point, RerouteOf(pin), SlotPoint(slot), reach) is >= 0 and var segment)
					return (pin, segment);
			}
		}
		return (null, -1);
	}

	private (Statement? To, int Segment) ExecWireAt(Vec world) {
		if (Session is null) return (null, -1);
		var reach = 14 / Zoom;
		(Statement?, int) hit = (null, -1);
		ForEachExecLink(null, (from, to) => {
			if (hit.Item1 is not null || !Session.Layout.IsPlaced(to) || Session.Layout.IsHidden(to)) return;
			if (SlotsOf(to).FirstOrDefault(s => s.Kind == SlotKind.ExecIn) is not { } target) return;
			var segment = WireSegmentAt(world, SlotPoint(from), ExecThrough(to), SlotPoint(target), reach);
			if (segment >= 0) hit = (to, segment);
		});
		if (hit.Item1 is not null) return hit;
		foreach (var host in VisibleNodes()) {
			if (NodeShape.ExpansionOf(host) is not { } frame) continue;
			foreach (var (key, start, end) in FrameExecWires(host, frame.Callee)) {
				var segment = WireSegmentAt(world, start, ExecThrough(key), end, reach);
				if (segment >= 0) return (key, segment);
			}
		}
		return hit;
	}

	private static StreamGeometry Bezier(Point from, Point to) {

		var geometry = new StreamGeometry();
		using var g = geometry.Open();
		var reach = Math.Max(40, Math.Min(260, Math.Abs(to.X - from.X) * 0.55 + Math.Abs(to.Y - from.Y) * 0.1));
		g.BeginFigure(from, false);
		g.CubicBezierTo(new Point(from.X + reach, from.Y), new Point(to.X - reach, to.Y), to);
		g.EndFigure(false);
		return geometry;
	}

	private void DrawWires(DrawingContext context, CanvasTheme theme, Avalonia.Rect view, RegionInfo? only = null) {
		var session = Session!;
		var layout = session.Layout;
		var selected = session.Selection.ToHashSet();
		foreach (var node in VisibleNodes(only)) {
			foreach (var slot in SlotsOf(node)) {
				if (slot.Kind != SlotKind.DataIn || slot.In?.Source is not { } source) continue;
				if ((Session!.Layout.CallableSlot(slot.In) ?? SlotFor(source)) is not { } from || from.Node == node) continue;
				if (IsModuleSetupNode(from.Node)) {
					if (source.Binding is not null) DrawProxy(context, theme, source, SlotPoint(slot), PinStyle.ColorOf(source.Type), slot.In);
					continue;
				}
				if (layout.IsHidden(from.Node)) {
					var rail = FrameRailPoint(from);
					var target = SlotPoint(slot);
					var tint = PinStyle.ColorOf(source.Type);
					if (rail is { } start) DrawRoutedData(context, theme, slot.In, start, target, tint);
					else DrawProxy(context, theme, source, target, tint, slot.In);
					continue;
				}

				if (only is null && layout.Keys.RegionOf(from.Node) is { } fromRegion && layout.CollapsedRegions.Contains(fromRegion.Body)) continue;
				var a = SlotPoint(from);
				var b = SlotPoint(slot);
				if (!new Avalonia.Rect(Math.Min(a.X, b.X), Math.Min(a.Y, b.Y), Math.Abs(a.X - b.X) + 1, Math.Abs(a.Y - b.Y) + 1).Intersects(view)) continue;
				var color = PinStyle.ColorOf(source.Type);
				var otherRegion = layout.Keys.RegionOf(from.Node) != layout.Keys.RegionOf(node);
				if (only is null && layout.CallableSlot(slot.In) is null && source.Binding is not null && (source.Consumers.Count >= ProxyThreshold || otherRegion)) {
					DrawProxy(context, theme, source, b, color, slot.In);
					continue;
				}
				var dim = selected.Count > 0 && !selected.Contains(node) && !selected.Contains(from.Node);
				var bad = m_pinProblems.ContainsKey(slot.In);
				var pen = bad ? new Pen(theme.Error, 2.5, lineCap: PenLineCap.Round) : theme.WirePen(color, dim, slot.In == m_hoverWire ? 3.8 : 2);
				var through = RerouteOf(slot.In);
				context.DrawGeometry(null, pen, through.Count > 0 ? WireGeometry(a, through, b) : RoutedWire(a, b, from.Node, node));
				foreach (var point in through) context.DrawEllipse(new SolidColorBrush(Color.Parse(color)), new Pen(theme.NodeBody, 1.5), point, 4.5, 4.5);
			}
		}
		DrawExecWires(context, theme, view, only);
	}

	private static void DrawPinGlyph(DrawingContext context, Point p, LuaType type, Color color) {
		var brush = new SolidColorBrush(color);
		var pen = new Pen(brush, 1.8);
		var r = PinRadius;
		switch (PinStyle.ShapeOf(type)) {
			case PinShape.Diamond:
				var diamond = new StreamGeometry();
				using (var g = diamond.Open()) {
					g.BeginFigure(new Point(p.X, p.Y - r - 1), true);
					g.LineTo(new Point(p.X + r + 1, p.Y));
					g.LineTo(new Point(p.X, p.Y + r + 1));
					g.LineTo(new Point(p.X - r - 1, p.Y));
					g.EndFigure(true);
				}
				context.DrawGeometry(brush, pen, diamond);
				break;
			case PinShape.Triangle:
				var triangle = new StreamGeometry();
				using (var g = triangle.Open()) {
					g.BeginFigure(new Point(p.X - r, p.Y - r - 1), true);
					g.LineTo(new Point(p.X + r + 2, p.Y));
					g.LineTo(new Point(p.X - r, p.Y + r + 1));
					g.EndFigure(true);
				}
				context.DrawGeometry(brush, pen, triangle);
				break;
			case PinShape.Square or PinShape.Stack:
				context.DrawRectangle(brush, pen, new Avalonia.Rect(p.X - r, p.Y - r, r * 2, r * 2), 1.5, 1.5);
				break;
			case PinShape.Pair:
				editor.LuaEditor.Controls.PinGlyph.DrawPair(context, p, color, r * 0.65);
				break;
			default:
				context.DrawEllipse(brush, pen, p, r, r);
				break;
		}
	}

	private Avalonia.Rect ProxyRect(InputPin? pin, OutputPin source, Point target) {
		var width = CanvasTheme.Default.Format(source.Name, 11, CanvasTheme.Default.Text, true).Width + 14;
		var rect = new Avalonia.Rect(target.X - 16 - width, target.Y - 9, width, 18);
		return pin is not null && Session!.Layout.ProxyOffsets.TryGetValue(pin, out var offset) ? rect.Translate(new Vector(offset.X, offset.Y)) : rect;
	}

	private InputPin? ProxyAt(Vec world) {
		if (Session is null) return null;
		var layout = Session.Layout;
		foreach (var node in VisibleNodes()) {
			foreach (var slot in SlotsOf(node)) {
				if (slot.Kind != SlotKind.DataIn || slot.In?.Source is not { } source || (Session!.Layout.CallableSlot(slot.In) ?? SlotFor(source)) is not { } from || from.Node == node) continue;
				var tagged = layout.CallableSlot(slot.In) is null && (layout.IsHidden(from.Node) ? FrameRailPoint(from) is null : source.Binding is not null && (source.Consumers.Count >= ProxyThreshold || layout.Keys.RegionOf(from.Node) != layout.Keys.RegionOf(node)));
				if (tagged && ProxyRect(slot.In, source, SlotPoint(slot)).Inflate(3).Contains(new Point(world.X, world.Y))) return slot.In;
			}
		}
		return null;
	}

	private void DrawProxy(DrawingContext context, CanvasTheme theme, OutputPin source, Point target, string color, InputPin? pin = null) {
		var text = theme.Format(source.Name, 11, theme.Text, true);
		var rect = ProxyRect(pin, source, target);
		var c = Color.Parse(color);
		var selected = pin is not null && pin == m_selectedProxy;
		context.DrawGeometry(null, new Pen(new SolidColorBrush(c, 0.9), 2, lineCap: PenLineCap.Round), Bezier(new Point(rect.Right, rect.Center.Y), target));
		DrawPinGlyph(context, new Point(rect.Right, rect.Center.Y), source.Type, c);
		context.DrawRectangle(new SolidColorBrush(c, 0.28), selected ? theme.SelectedBorder : new Pen(new SolidColorBrush(c, 0.9), 1), rect, 9, 9);

		context.DrawText(text, new Point(rect.X + 7, rect.Y + (rect.Height - text.Height) / 2));
	}

	private void DrawExecWires(DrawingContext context, CanvasTheme theme, Avalonia.Rect view, RegionInfo? only = null) => ForEachExecLink(only, (from, to) => DrawExec(context, theme, from, to, view));

	private void ForEachExecLink(RegionInfo? only, Action<Slot, Statement> link) {
		var layout = Session!.Layout;
		foreach (var region in CanvasRegions()) {
			if (only is not null && region != only) continue;
			if (only is null && layout.CollapsedRegions.Contains(region.Body)) continue;
			if (region.Entry is not null && !InlineRules.IsMerged(region.Entry) && layout.IsPlaced(region.Entry) && !layout.IsHidden(region.Entry)) {
				var slot = SlotsOf(region.Entry).FirstOrDefault(s => s.Kind == SlotKind.ExecOut);
				if (slot is not null) LinkBlock(link, slot, region.Body);
			}
			foreach (var node in layout.NodesOf(region)) {
				if (node is not Statement statement || InlineRules.IsMerged(node) || !layout.IsPlaced(node) || layout.IsHidden(node) || statement.Parent is null) continue;
				var slots = SlotsOf(statement);
				foreach (var slot in slots.Where(s => s.Kind == SlotKind.ExecOut && s.Child is not null)) LinkBlock(link, slot, slot.Child!);
				if (!NodeShape.HasContinuation(statement)) continue;
				var next = NextInChain(statement);
				var continuation = slots.LastOrDefault(s => s.Kind == SlotKind.ExecOut && s.Child is null);
				if (next is null || continuation is null) continue;
				link(continuation, next);
			}
		}
	}

	private static Statement? NextInChain(Statement statement) {
		var block = statement.Parent!;
		for (var i = block.IndexOf(statement) + 1; i < block.Statements.Count; i++)
			if (NodeShape.HasExecutionIn(block.Statements[i]) && !block.Statements[i].IsFloating) return block.Statements[i];
		return null;
	}

	private static void LinkBlock(Action<Slot, Statement> link, Slot from, Block block) {
		var first = block.Statements.FirstOrDefault(s => !s.IsFloating && NodeShape.HasExecutionIn(s));
		if (first is not null) link(from, first);
	}

	private void DrawExec(DrawingContext context, CanvasTheme theme, Slot from, Statement to, Avalonia.Rect view) {
		if (!Session!.Layout.IsPlaced(to) || Session.Layout.IsHidden(to)) return;
		var target = SlotsOf(to).FirstOrDefault(s => s.Kind == SlotKind.ExecIn);
		if (target is null) return;
		var a = SlotPoint(from);
		var b = SlotPoint(target);
		if (!new Avalonia.Rect(Math.Min(a.X, b.X), Math.Min(a.Y, b.Y), Math.Abs(a.X - b.X) + 1, Math.Abs(a.Y - b.Y) + 1).Intersects(view)) return;
		var through = ExecThrough(to);
		context.DrawGeometry(null, theme.ExecWire, through.Count > 0 ? WireGeometry(a, through, b) : Bezier(a, b));
		foreach (var point in through) context.DrawEllipse(Brushes.White, new Pen(theme.NodeBody, 1.5), point, 4.5, 4.5);
	}

	public static Avalonia.Rect ToggleButtonRect(Avalonia.Rect node) => new(node.X + 6, node.Y + 4, 22, 20);

	private void DrawToggleButton(DrawingContext context, CanvasTheme theme, Avalonia.Rect node, bool open, IBrush ink) {
		var button = ToggleButtonRect(node);
		context.DrawRectangle(new SolidColorBrush(Color.Parse("#000000"), 0.28), new Pen(new SolidColorBrush(Color.Parse("#ffffff"), 0.55), 1.2), button, 5, 5);
		var cx = button.X + button.Width / 2;
		var cy = button.Y + button.Height / 2;
		var dy = open ? -2.5 : 2.5;
		var arrow = new StreamGeometry();
		using (var g = arrow.Open()) {
			g.BeginFigure(new Point(cx - 5, cy - dy), false);
			g.LineTo(new Point(cx, cy + dy));
			g.LineTo(new Point(cx + 5, cy - dy));
			g.EndFigure(false);
		}
		context.DrawGeometry(null, new Pen(ink, 2, lineCap: PenLineCap.Round, lineJoin: PenLineJoin.Round), arrow);
	}

	private void DrawNodes(DrawingContext context, CanvasTheme theme, Avalonia.Rect view, RegionInfo? only = null) {
		var session = Session!;
		var selected = session.Selection.ToHashSet();
		var detailed = Zoom >= 0.4;
		foreach (var node in VisibleNodes(only)) {
			var rect = NodeRect(node);
			if (!rect.Intersects(view)) continue;
			if (NodeShape.ExpansionOf(node) is not null) continue;
			var look = NodeShape.LookOf(node);
			if (node is CommentStatement) {
				DrawNote(context, theme, node, rect, selected.Contains(node));
				continue;
			}
			var border = selected.Contains(node) ? theme.SelectedBorder
				: m_nodeSeverity.TryGetValue(node, out var severity) ? severity == DiagnosticSeverity.Error ? theme.ErrorBorder : theme.WarningBorder
				: theme.NodeBorder;
			if (InlineRules.IsGetterNode(node)) {
				DrawGetterTag(context, theme, node, rect, border, detailed);
				continue;
			}
			var loose = session.Layout.Loose.Contains(node);
			context.DrawRectangle(loose ? theme.NodeBodyDim : theme.NodeBody, border, rect, 7, 7);

			context.DrawRectangle(theme.HeaderOf(look), null, new Avalonia.Rect(rect.X + 1, rect.Y + 1, rect.Width - 2, DefaultMeasure.HeaderHeight - 2), 6, 6);
			if (!detailed) continue;
			var openable = session.CanOpen(node);
			if (openable) DrawToggleButton(context, theme, rect, open: false, theme.HeaderTextOf(look));
			context.DrawText(theme.Format(NodeShape.TitleOf(node), 12.5, theme.HeaderTextOf(look), true), new Point(rect.X + (openable ? 36 : 10), rect.Y + 6));
			foreach (var slot in SlotsOf(node)) DrawSlot(context, theme, slot, rect);
			if (node is TableNode table) DrawTableAdd(context, theme, table, rect);
		}
	}

	public static Avalonia.Rect TableAddRect(Avalonia.Rect node) => new(node.X + 10, node.Bottom - 26, 96, 20);

	public static TableFieldKind TableGrowsBy(TableNode table) => GraphSession.EntryKindOf(table);

	private void DrawTableAdd(DrawingContext context, CanvasTheme theme, TableNode table, Avalonia.Rect rect) {
		var button = TableAddRect(rect);
		context.DrawRectangle(new SolidColorBrush(Color.Parse("#ffffff"), 0.10), new Pen(new SolidColorBrush(Color.Parse("#ffffff"), 0.35), 1), button, 5, 5);
		var text = theme.Format(table.Flavor switch { TableFlavor.Array => "+  item", TableFlavor.Dictionary => "+  entry", _ => "+  field" }, 11, theme.Text, true);
		context.DrawText(text, new Point(button.X + 10, button.Y + (button.Height - text.Height) / 2));
	}

	private void DrawGetterTag(DrawingContext context, CanvasTheme theme, GraphNode node, Avalonia.Rect rect, Pen border, bool detailed) {
		var slot = SlotsOf(node).FirstOrDefault(s => s.Kind == SlotKind.DataOut);
		var type = slot?.Out?.Type ?? LuaType.Any;
		var tint = Color.Parse(PinStyle.ColorOf(type));
		var selected = Session!.Selection.Contains(node);
		context.DrawRectangle(new SolidColorBrush(tint, 0.28), selected ? theme.SelectedBorder : new Pen(new SolidColorBrush(tint, 0.9), 1), rect, rect.Height / 2, rect.Height / 2);
		if (detailed) {
			var text = theme.Format(NodeShape.TitleOf(node), 11, theme.Text, true);
			context.DrawText(text, new Point(rect.X + 10, rect.Y + (rect.Height - text.Height) / 2));
		}
		foreach (var pin in SlotsOf(node)) DrawSlot(context, theme, pin, rect);
	}

	private void DrawFrames(DrawingContext context, CanvasTheme theme, Avalonia.Rect view) {
		var selected = Session!.Selection.ToHashSet();
		foreach (var node in VisibleNodes()) {
			if (NodeShape.ExpansionOf(node) is not { } frame) continue;
			var rect = NodeRect(node);
			if (rect.Intersects(view)) DrawFrame(context, theme, node, rect, frame.Callee, selected.Contains(node));
		}
	}

	private Point? FrameRailPoint(Slot entryOutput) {
		var layout = Session!.Layout;
		if (entryOutput.Out?.Owner is not FunctionEntry entry || layout.Keys.RegionOf(entry) is not { } region || !layout.Embedded.TryGetValue(region.Body, out var host)) return null;
		var index = entry.Params.FindIndex(p => p.Output == entryOutput.Out);
		var rows = SlotsOf(host).Where(s => s.Kind == SlotKind.DataIn).ToList();
		if (index < 0 || index >= rows.Count) return null;
		var rect = NodeRect(host);
		return new Point(rect.X + DefaultMeasure.Rails(host).Left, SlotPoint(rows[index]).Y);
	}

	private void DrawFrame(DrawingContext context, CanvasTheme theme, GraphNode node, Avalonia.Rect rect, FunctionDecl callee, bool selected) {
		var layout = Session!.Layout;
		var look = NodeShape.LookOf(node);
		var accent = theme.HeaderOf(look);
		var accentColor = accent is ISolidColorBrush solid ? solid.Color : Color.Parse("#29adff");
		var (railLeft, railRight) = DefaultMeasure.Rails(node);

		context.DrawRectangle(new SolidColorBrush(Color.Parse("#1c1b20")), selected ? theme.SelectedBorder : new Pen(new SolidColorBrush(accentColor, 0.75), 2), rect, 8, 8);
		context.DrawRectangle(accent, null, new Avalonia.Rect(rect.X + 1, rect.Y + 1, rect.Width - 2, DefaultMeasure.HeaderHeight - 2), 7, 7);
		DrawToggleButton(context, theme, rect, open: true, theme.HeaderTextOf(look));
		context.DrawText(theme.Format(callee.Name + "()", 12.5, theme.HeaderTextOf(look), true), new Point(rect.X + 36, rect.Y + 6));

		var body = new Avalonia.Rect(rect.X + 1, rect.Y + DefaultMeasure.HeaderHeight, rect.Width - 2, rect.Height - DefaultMeasure.HeaderHeight - 1);
		var rail = new SolidColorBrush(Color.Parse("#454343"));
		context.DrawRectangle(rail, null, new Avalonia.Rect(body.X, body.Y, railLeft, body.Height));
		context.DrawRectangle(rail, null, new Avalonia.Rect(body.Right - railRight, body.Y, railRight, body.Height));
		var floor = new Avalonia.Rect(body.X + railLeft, body.Y, body.Width - railLeft - railRight, body.Height);
		context.DrawRectangle(new SolidColorBrush(Color.Parse("#121115")), null, floor);
		var line = new Pen(new SolidColorBrush(Color.Parse("#ffffff"), 0.05), 1);
		for (var x = floor.X + 32; x < floor.Right; x += 32) context.DrawLine(line, new Point(x, floor.Y), new Point(x, floor.Bottom));
		for (var y = floor.Y + 32; y < floor.Bottom; y += 32) context.DrawLine(line, new Point(floor.X, y), new Point(floor.Right, y));

		foreach (var slot in SlotsOf(node)) {
			DrawSlot(context, theme, slot, rect);
			if (slot.IsCallableReference || slot.Kind == SlotKind.DataOut) continue;
			var inner = FrameExecPoint(slot);
			if (slot.IsExec) DrawArrow(context, theme, inner, connected: true, hot: false);
			else {
				var type = slot.In?.Source?.Type ?? slot.In?.Expected ?? LuaType.Any;
				context.DrawEllipse(new SolidColorBrush(Color.Parse(PinStyle.ColorOf(type))), new Pen(rail, 1.5), inner, 5, 5);
			}
		}
		foreach (var (key, start, end) in FrameExecWires(node, callee)) {
			var bends = ExecThrough(key);
			context.DrawGeometry(null, theme.ExecWire, WireGeometry(start, bends, end));
			foreach (var point in bends) context.DrawEllipse(Brushes.White, new Pen(theme.NodeBody, 1.5), point, 4.5, 4.5);
		}
		foreach (var port in FrameReturns(node, callee)) {
			var color = new SolidColorBrush(Color.Parse(PinStyle.ColorOf(port.Input?.Source?.Type ?? port.Input?.Expected ?? LuaType.Any)));
			context.DrawEllipse(port.Input is null ? rail : color, new Pen(color, 1.5), port.Point, 5, 5);
			if (port.Input is { } input) DrawFrameReturn(context, theme, input, port.Point);
			else {
				var label = theme.Format("return", 11, theme.TextMuted);
				context.DrawText(label, new Point(port.Point.X + 9, port.Point.Y - label.Height / 2));
			}
		}
	}

	private IEnumerable<(Statement Key, Point Start, Point End)> FrameExecWires(GraphNode host, FunctionDecl function) {
		var layout = Session!.Layout;
		var slots = SlotsOf(host);
		var body = function.Entry.Body;
		var enter = slots.FirstOrDefault(s => s.Kind == SlotKind.ExecIn);
		var exit = slots.FirstOrDefault(s => s.Kind == SlotKind.ExecOut && s.Child is null);
		var first = body.Statements.FirstOrDefault(s => !s.IsFloating && NodeShape.HasExecutionIn(s) && !layout.IsHidden(s));
		if (enter is not null && first is not null && layout.IsPlaced(first) &&
			SlotsOf(first).FirstOrDefault(s => s.Kind == SlotKind.ExecIn) is { } entry)
			yield return (first, FrameExecPoint(enter), SlotPoint(entry));
		var last = FrameTail(body);
		if (exit is not null && last is not null && layout.IsPlaced(last) &&
			SlotsOf(last).LastOrDefault(s => s.Kind == SlotKind.ExecOut && s.Child is null) is { } tail)
			yield return (function, SlotPoint(tail), FrameExecPoint(exit));
		else if (first is null && enter is not null && exit is not null)
			yield return (function, FrameExecPoint(enter), FrameExecPoint(exit));
	}

	private void DrawRoutedData(DrawingContext context, CanvasTheme theme, InputPin pin, Point start, Point end, string color) {
		var bends = RerouteOf(pin);
		context.DrawGeometry(null, theme.WirePen(color, width: pin == m_hoverWire ? 3.8 : 2), WireGeometry(start, bends, end));
		foreach (var point in bends)
			context.DrawEllipse(new SolidColorBrush(Color.Parse(color)), new Pen(theme.NodeBody, 1.5), point, 4.5, 4.5);
	}

	private void DrawFrameReturn(DrawingContext context, CanvasTheme theme, InputPin value, Point end) {
		if (value.Source is { } source) {
			var color = PinStyle.ColorOf(source.Type);
			if (FrameReturnSource(value) is { } point) DrawRoutedData(context, theme, value, point, end, color);
			else DrawProxy(context, theme, source, end, color, value);
		} else if (value.Constant is { } literal) {
			var text = theme.Format(literal.Raw, 11, theme.Text);
			context.DrawText(text, new Point(end.X - text.Width - 9, end.Y - text.Height / 2));
		}
	}

	private void DrawNote(DrawingContext context, CanvasTheme theme, GraphNode node, Avalonia.Rect rect, bool selected) {
		context.DrawRectangle(new SolidColorBrush(Color.Parse("#665f10"), 0.4), selected ? theme.SelectedBorder : new Pen(new SolidColorBrush(Color.Parse("#ffec27"), 0.5), 1), new Avalonia.Rect(rect.X, rect.Y, rect.Width, DefaultMeasure.HeaderHeight + 4), 6, 6);
		if (Zoom >= 0.4) context.DrawText(theme.Format(NodeShape.TitleOf(node), 12, theme.Text), new Point(rect.X + 10, rect.Y + 7));
	}

	private void DrawSlot(DrawingContext context, CanvasTheme theme, Slot slot, Avalonia.Rect rect) {
		var p = SlotPoint(slot);
		var hot = m_hoverSlot is { } h && h.Node == slot.Node && h.Row == slot.Row && h.Kind == slot.Kind;
		if (slot.IsExec) {
			var connected = IsExecConnected(slot);
			DrawArrow(context, theme, p, connected, hot);
			if (slot.Label.Length > 0) DrawLabel(context, theme, slot, p);
			return;
		}

		var type = PinTypeOf(slot);
		var color = Color.Parse(PinStyle.ColorOf(type));
		var connectedData = (slot.In?.Source is not null && InlineRules.EnumValueOf(slot.In) is null) || (slot.Out is { } o && o.Consumers.Count > 0);
		var brush = connectedData ? new SolidColorBrush(color) : null;
		var pen = new Pen(new SolidColorBrush(color), hot ? 2.5 : 1.8);
		var radius = hot ? PinRadius + 1.5 : PinRadius;
		switch (PinStyle.ShapeOf(type)) {
			case PinShape.Diamond:
				var diamond = new StreamGeometry();
				using (var g = diamond.Open()) {
					g.BeginFigure(new Point(p.X, p.Y - radius - 1), true);
					g.LineTo(new Point(p.X + radius + 1, p.Y));
					g.LineTo(new Point(p.X, p.Y + radius + 1));
					g.LineTo(new Point(p.X - radius - 1, p.Y));
					g.EndFigure(true);
				}
				context.DrawGeometry(brush ?? theme.NodeBody, pen, diamond);
				break;
			case PinShape.Pair:
				editor.LuaEditor.Controls.PinGlyph.DrawPair(context, p, color, radius * 0.65, brush is not null);
				break;
			case PinShape.Hexagon:
				var hexagon = new StreamGeometry();
				using (var g = hexagon.Open()) {
					var r = radius + 1.5;
					g.BeginFigure(new Point(p.X - r, p.Y), true);
					g.LineTo(new Point(p.X - r / 2, p.Y - r * 0.87));
					g.LineTo(new Point(p.X + r / 2, p.Y - r * 0.87));
					g.LineTo(new Point(p.X + r, p.Y));
					g.LineTo(new Point(p.X + r / 2, p.Y + r * 0.87));
					g.LineTo(new Point(p.X - r / 2, p.Y + r * 0.87));
					g.EndFigure(true);
				}
				context.DrawGeometry(brush ?? theme.NodeBody, pen, hexagon);
				break;
			case PinShape.Triangle:
				var triangle = new StreamGeometry();
				using (var g = triangle.Open()) {
					g.BeginFigure(new Point(p.X - radius, p.Y - radius - 1), true);
					g.LineTo(new Point(p.X + radius + 2, p.Y));
					g.LineTo(new Point(p.X - radius, p.Y + radius + 1));
					g.EndFigure(true);
				}
				context.DrawGeometry(brush ?? theme.NodeBody, pen, triangle);
				break;
			case PinShape.Stack:
				var back = new Avalonia.Rect(p.X - radius, p.Y - radius, radius * 2, radius * 2);
				context.DrawRectangle(brush ?? theme.NodeBody, pen, back, 1.5, 1.5);
				var inkPen = new Pen(brush is null ? new SolidColorBrush(color) : theme.NodeBody, 1.1);
				context.DrawLine(inkPen, new Point(p.X - radius + 1.5, p.Y), new Point(p.X + radius - 1.5, p.Y));
				context.DrawLine(inkPen, new Point(p.X, p.Y - radius + 1.5), new Point(p.X, p.Y + radius - 1.5));
				break;
			case PinShape.Square:
				context.DrawRectangle(brush ?? theme.NodeBody, pen, new Avalonia.Rect(p.X - radius, p.Y - radius, radius * 2, radius * 2), 1.5, 1.5);
				break;
			default:
				context.DrawEllipse(brush ?? theme.NodeBody, pen, p, radius, radius);
				break;
		}

		if (PinStyle.IsOptional(type)) context.DrawEllipse(Brushes.Black, null, p, 2.4, 2.4);
		if (!InlineRules.IsGetterNode(slot.Node)) DrawLabel(context, theme, slot, p);
if (slot.In is { } pin && (pin.Source is null || InlineRules.InlineValueOf(pin) is not null)) DrawConstant(context, theme, slot, pin, p);
	}

	private static LuaType PinTypeOf(Slot slot) {
		if (slot.In?.Source?.Type is { } wired) return wired;
		var expected = slot.In?.Expected;
		if (expected is not null && expected is not PrimitiveType { Kind: PrimitiveKind.Any or PrimitiveKind.Unknown }) return expected;
		if (slot.In?.Constant is { } constant)
			return constant.Kind switch {
				ConstantKind.Number => LuaType.Number,
				ConstantKind.String => LuaType.String,
				ConstantKind.Boolean => LuaType.Boolean,
				_ => expected ?? LuaType.Any
			};
		return expected ?? slot.Out?.Type ?? LuaType.Any;
	}

	private Statement? FrameTail(Block body) => body.Statements.LastOrDefault(s =>
		!s.IsFloating && NodeShape.HasExecutionIn(s) && !Session!.Layout.IsHidden(s)) is { } tail && NodeShape.HasContinuation(tail) ? tail : null;

	private bool IsExecConnected(Slot slot) {
		if (slot.Node is Statement { IsFloating: true }) return false;
		if (slot.Kind == SlotKind.ExecIn) return slot.Node is Statement { Parent: not null };
		if (slot.Child is { } child) return child.Statements.Any(NodeShape.HasExecutionIn);
		return slot.Node is Statement s && (NextInChain(s) is not null ||
			s.Parent is { } body && Session!.Layout.Embedded.ContainsKey(body) && FrameTail(body) == s);
	}

	private static void DrawArrow(DrawingContext context, CanvasTheme theme, Point p, bool connected, bool hot) {
		var geometry = new StreamGeometry();
		using (var g = geometry.Open()) {
			g.BeginFigure(new Point(p.X - 5, p.Y - 6), true);
			g.LineTo(new Point(p.X + 4, p.Y - 6));
			g.LineTo(new Point(p.X + 8, p.Y));
			g.LineTo(new Point(p.X + 4, p.Y + 6));
			g.LineTo(new Point(p.X - 5, p.Y + 6));
			g.EndFigure(true);
		}
		context.DrawGeometry(connected ? theme.Exec : null, new Pen(theme.Exec, hot ? 2.5 : 1.6), geometry);
	}

	private void DrawLabel(DrawingContext context, CanvasTheme theme, Slot slot, Point p) {
		if (slot.Label.Length == 0) return;
		if (slot.In is { } valuePin && KeyOfRow(valuePin) is { } key) {
			var keyText = theme.Format(NodeShape.KeyText(key), 11, key.Constant?.Kind == ConstantKind.String ? new SolidColorBrush(Color.Parse(PinStyle.Text)) : theme.Text);
			var keyBox = KeyBoxRect(slot, p);
			context.DrawRectangle(theme.ConstantBox, new Pen(m_pinProblems.ContainsKey(key) ? theme.Error : new SolidColorBrush(Color.Parse("#454343")), 1), keyBox, 3, 3);
			context.DrawText(keyText, new Point(keyBox.X + 5, keyBox.Y + (keyBox.Height - keyText.Height) / 2));
			return;
		}
		var text = theme.Format(slot.Label, 11.5, theme.TextMuted);
		var x = slot.IsLeft ? p.X + 12 : p.X - 12 - text.Width;
		context.DrawText(text, new Point(x, p.Y - text.Height / 2));
	}

	private static InputPin? KeyOfRow(InputPin value) => value.Owner is TableNode table && table.Fields.FirstOrDefault(f => f.Value == value) is { Kind: TableFieldKind.Keyed, Key: { Source: null } key } ? key : null;

	private static Avalonia.Rect KeyBoxRect(Slot slot, Point p) {
		var width = CanvasTheme.Default.Format(slot.Label, 11.5, CanvasTheme.Default.TextMuted).Width;
		return new Avalonia.Rect(p.X + 10, p.Y - 8, width + 4, 16);
	}

	private void DrawConstant(DrawingContext context, CanvasTheme theme, Slot slot, InputPin pin, Point p) {
		var label = theme.Format(slot.Label, 11.5, theme.TextMuted);
		var box = ConstantRect(slot, p, label.Width);
		if (GraphSession.IsBoolean(pin) && pin.Source is null) {
			var check = new Avalonia.Rect(box.X, p.Y - 7, 14, 14);
			var on = pin.Constant?.Raw == "true";
			context.DrawRectangle(on ? new SolidColorBrush(Color.Parse(PinStyle.Boolean)) : theme.ConstantBox, new Pen(new SolidColorBrush(Color.Parse(PinStyle.Boolean)), 1.2), check, 3, 3);
			if (on) context.DrawText(theme.Format("✓", 11, new SolidColorBrush(Color.Parse("#121111")), true), new Point(check.X + 2.5, check.Y - 0.5));
			context.DrawText(theme.Format(on ? "true" : "false", 11, theme.TextMuted), new Point(check.Right + 6, p.Y - 7));
			return;
		}
		var enumValue = InlineRules.EnumValueOf(pin);
		var isList = enumValue is not null || (Session?.ChoicesFor(pin) is not null);
		var text = enumValue ?? (pin.Constant is { } literal ? GraphSession.DisplayText(literal) : "nil");
		var room = (int)((DefaultMeasure.ConstantWidth(pin) - (isList ? 20 : 10)) / 6.0);
		if (text.Length > room) text = text[..Math.Max(1, room - 1)] + "…";
		var problem = m_pinProblems.ContainsKey(pin);
		context.DrawRectangle(theme.ConstantBox, new Pen(problem ? theme.Error : new SolidColorBrush(Color.Parse("#454343")), 1), box, 3, 3);
		var value = theme.Format(text, 11, pin.Constant is null ? theme.TextMuted : pin.Constant.Kind == ConstantKind.String ? new SolidColorBrush(Color.Parse(PinStyle.Text)) : theme.Text);
		context.DrawText(value, new Point(box.X + 5, box.Y + (box.Height - value.Height) / 2));
		if (isList) context.DrawText(theme.Format("▾", 10, theme.TextMuted), new Point(box.Right - 13, box.Y + 1.5));
	}

	private static Avalonia.Rect ConstantRect(Slot slot, Point p, double labelWidth) => new(p.X + 18 + labelWidth, p.Y - 8, slot.In is { } pin ? DefaultMeasure.ConstantWidth(pin) : ConstantBoxWidth, 16);

	private void DrawDragPreview(DrawingContext context, CanvasTheme theme) {
		switch (m_drag) {
			case DragMode.Wire when m_wireFromOutput is not null || m_wireFromInput is not null || m_wireFromReturn is not null: {
				var anchorSlot = m_wireFromOutput is not null ? SlotFor(m_wireFromOutput) : m_wireFromInput is { } input ? SlotFor(input) : null;
				if (anchorSlot is null && m_wireAnchor is null) break;
				var anchor = m_wireAnchor ?? SlotPoint(anchorSlot!);
				var type = m_wireFromOutput?.Type ?? m_wireFromInput?.Expected ?? LuaType.Any;
				var ok = (m_hoverTarget is null && m_hoverRail is null) || m_hoverCompat.Kind != Compat.Blocked;
				var pen = ok ? theme.WirePen(PinStyle.ColorOf(type)) : new Pen(theme.Error, 2.5, lineCap: PenLineCap.Round);
				var end = m_hoverRail ?? (m_hoverTarget is { } target ? SlotPoint(target) : new Point(m_pointerWorld.X, m_pointerWorld.Y));
				context.DrawGeometry(null, pen, m_wireFromOutput is not null ? Bezier(anchor, end) : Bezier(end, anchor));
				if ((m_hoverTarget is not null || m_hoverRail is not null) && m_hoverCompat.Reason is { } reason) {
					var tip = theme.Format(reason, 11.5, m_hoverCompat.Kind == Compat.Blocked ? theme.Error : theme.Warning);
					context.DrawRectangle(theme.NodeBody, null, new Avalonia.Rect(end.X + 12, end.Y - 10, tip.Width + 12, tip.Height + 6), 4, 4);
					context.DrawText(tip, new Point(end.X + 18, end.Y - 7));
				}
				break;
			}
			case DragMode.Exec when m_execFrom is not null || m_execFromFrameExit is not null: {
				var a = m_wireAnchor ?? SlotPoint(m_execFrom!);
				var b = m_hoverExecTarget is { } t && SlotsOf(t).FirstOrDefault(s => s.Kind == SlotKind.ExecIn) is { } slot ? SlotPoint(slot) : new Point(m_pointerWorld.X, m_pointerWorld.Y);
				if (m_execFromFrameExit is not null) context.DrawGeometry(null, theme.ExecWire, Bezier(b, a));
				else context.DrawGeometry(null, theme.ExecWire, Bezier(a, FrameExitAt(m_pointerWorld)?.Point ?? b));
				break;
			}
		}
	}

	private void DrawBoxSelect(DrawingContext context, CanvasTheme theme) {
		if (m_drag != DragMode.Box) return;
		var r = new Avalonia.Rect(Math.Min(m_boxStart.X, m_pointerScreen.X), Math.Min(m_boxStart.Y, m_pointerScreen.Y), Math.Abs(m_boxStart.X - m_pointerScreen.X), Math.Abs(m_boxStart.Y - m_pointerScreen.Y));
		context.DrawRectangle(new SolidColorBrush(Color.Parse("#29adff"), 0.12), new Pen(theme.Accent, 1, dashStyle: DashStyle.Dash), r);
	}

	private Avalonia.Rect MinimapRect(Size size) => new(size.Width - MinimapWidth - 12, size.Height - MinimapHeight - 12, MinimapWidth, MinimapHeight);

	private const double MinimapWidth = 190;
	private const double MinimapHeight = 120;

	private (Avalonia.Rect World, double Scale)? MinimapWorld(Avalonia.Rect map) {
		var nodes = VisibleNodes().ToList();
		if (nodes.Count == 0) return null;
		var world = nodes.Select(NodeRect).Concat(VisibleGroups().Select(g => new Avalonia.Rect(g.Rect.X, g.Rect.Y, g.Rect.W, g.Rect.H))).Aggregate((a, b) => a.Union(b)).Inflate(40);
		var scale = Math.Min(map.Width / world.Width, map.Height / world.Height);
		return (world, scale);
	}

	private void DrawMinimap(DrawingContext context, CanvasTheme theme, Size size) {
		if (!MinimapVisible || Session is null) return;
		var map = MinimapRect(size);
		context.DrawRectangle(new SolidColorBrush(Color.Parse("#121111"), 0.85), new Pen(new SolidColorBrush(Color.Parse("#454343")), 1), map, 5, 5);
		if (MinimapWorld(map) is not { } info) return;
		var (world, scale) = info;
		Point ToMap(double x, double y) => new(map.X + (x - world.X) * scale + (map.Width - world.Width * scale) / 2, map.Y + (y - world.Y) * scale + (map.Height - world.Height * scale) / 2);
		using var clip = context.PushClip(map);
		foreach (var group in VisibleGroups()) {
			var a = ToMap(group.Rect.X, group.Rect.Y);
			var color = Color.Parse(group.Color);
			context.DrawRectangle(new SolidColorBrush(color, 0.45), new Pen(new SolidColorBrush(color, 1), 1.5), new Avalonia.Rect(a.X, a.Y, Math.Max(4, group.Rect.W * scale), Math.Max(4, group.Rect.H * scale)));
		}
		foreach (var node in VisibleNodes()) {
			var r = NodeRect(node);
			var a = ToMap(r.X, r.Y);
			context.DrawRectangle(theme.HeaderOf(NodeShape.LookOf(node)), null, new Avalonia.Rect(a.X, a.Y, Math.Max(2, r.Width * scale), Math.Max(2, r.Height * scale)));
		}
		var tl = ToMap(-PanX / Zoom, -PanY / Zoom);
		context.DrawRectangle(null, new Pen(theme.Accent, 1.2), new Avalonia.Rect(tl.X, tl.Y, size.Width / Zoom * scale, size.Height / Zoom * scale));
	}
}
