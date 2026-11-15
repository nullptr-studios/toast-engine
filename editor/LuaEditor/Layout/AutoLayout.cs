using System;
using System.Collections.Generic;
using System.Linq;
using editor.LuaEditor.Graph;

namespace editor.LuaEditor.Layout;

public sealed class AutoLayout {
	public const double ColumnGap = 64;
	public const double StepGap = 90;
	public const double RowGap = 46;

	public const double MaxRowWidth = 1e9;

	public const double ExecClearance = DefaultMeasure.HeaderHeight + DefaultMeasure.RowHeight + 12;

	private readonly INodeMeasure m_measure;
	private readonly GraphKeys m_keys;
	private readonly Dictionary<GraphNode, Vec> m_result = [];

	public AutoLayout(INodeMeasure measure, GraphKeys keys) {
		m_measure = measure;
		m_keys = keys;
	}

	public Dictionary<GraphNode, Vec> Region(RegionInfo region) {
		m_result.Clear();
		var x = 0.0;
		if (region.Header is FunctionDecl decl) {
			var size = m_measure.Measure(decl);
			var data = DataColumns(decl, 0, 0, out var dataBottom, out var dataLeft, 0);
			Put(decl, new Vec(dataLeft, 0));
			x = dataLeft + size.W + StepGap;
			_ = data;
			_ = dataBottom;
		} else if (region.Entry is not null) {
			x = PlaceEntry(region, x);
		}
		Block(region.Body, x, 0, x + MaxRowWidth, out _, out _);

		var minX = m_result.Count == 0 ? 0 : m_result.Values.Min(v => v.X);
		var minY = m_result.Count == 0 ? 0 : m_result.Values.Min(v => v.Y);
		return m_result.ToDictionary(p => p.Key, p => new Vec(p.Value.X - minX, p.Value.Y - minY));
	}

	public Size SizeOf(Dictionary<GraphNode, Vec> placed) {
		var w = 0.0;
		var h = 0.0;
		foreach (var (node, p) in placed) {
			var s = m_measure.Measure(node);
			w = Math.Max(w, p.X + s.W);
			h = Math.Max(h, p.Y + s.H);
		}
		return new Size(w, h);
	}

	private double PlaceEntry(RegionInfo region, double x) {
		var entry = region.Entry!;
		Put(entry, new Vec(x, 0));
		return x + m_measure.Measure(entry).W + StepGap;
	}

	private void Put(GraphNode node, Vec position) => m_result[node] = position;

	private void Block(Block block, double x, double y, double limit, out double right, out double bottom) {
		right = x;
		bottom = y;
		var cursor = x;
		var rowY = y;
		var inRow = 0;
		foreach (var statement in block.Statements) {
			if (statement is FunctionDecl) continue;
			var size = m_measure.Measure(statement);
			DataColumns(statement, cursor, rowY, out var dataBottom, out var statementX);
			if (inRow > 0 && statementX + size.W > limit) {
				cursor = x;
				rowY = bottom + RowGap * 2;
				inRow = 0;
				DataColumns(statement, cursor, rowY, out dataBottom, out statementX);
			}
			Put(statement, new Vec(statementX, rowY));

			var rowBottom = Math.Max(rowY + size.H, dataBottom);
			var rowRight = statementX + size.W;
			var childY = rowY;
var first = true;
			foreach (var child in statement.ChildBlocks) {
				if (!first) childY += RowGap;
				first = false;
				Block(child, statementX + size.W + StepGap, childY, limit, out var childRight, out var childBottom);
				// empty branches still need room for a flow wire
				childBottom = Math.Max(childBottom, childY + DefaultMeasure.RowHeight);
				rowRight = Math.Max(rowRight, childRight);
				childY = childBottom;
				rowBottom = Math.Max(rowBottom, childBottom);
			}
			right = Math.Max(right, rowRight);
			bottom = Math.Max(bottom, rowBottom);
			if (statement.ChildBlocks.Any() && NodeShape.HasContinuation(statement)) {
				// branch order keeps the flow wires from crossing
				cursor = statementX + size.W + StepGap;
				rowY = rowBottom + RowGap;
			} else {
				cursor = rowRight + StepGap;
			}
			inRow++;
		}
	}

	private List<(GraphNode Node, int Depth)> DataColumns(Statement statement, double left, double top, out double bottom, out double statementX, double clearance = ExecClearance) {
		var nodes = new List<(GraphNode Node, int Depth, double Y)>();
		var cursorY = top + clearance;
		var seen = new HashSet<GraphNode>();
		foreach (var pin in statement.Inputs) cursorY = Collect(pin, 1, cursorY, nodes, seen);
		bottom = Math.Max(cursorY - RowGap, top);

		var maxDepth = nodes.Count == 0 ? 0 : nodes.Max(n => n.Depth);
		var columnWidth = new double[maxDepth + 2];
		foreach (var (node, depth, _) in nodes) columnWidth[depth] = Math.Max(columnWidth[depth], m_measure.Measure(node).W);

		var columnX = new double[maxDepth + 2];
		var x = left;
		for (var d = maxDepth; d >= 1; d--) {
			columnX[d] = x;
			x += columnWidth[d] + ColumnGap;
		}
		statementX = x;
		foreach (var (node, depth, y) in nodes) Put(node, new Vec(columnX[depth] + columnWidth[depth] - m_measure.Measure(node).W, y));
		return nodes.Select(n => (n.Node, n.Depth)).ToList();
	}

	private double Collect(InputPin pin, int depth, double top, List<(GraphNode Node, int Depth, double Y)> nodes, HashSet<GraphNode> seen) {
		if (pin.Source?.Owner is not ExprNode expr || !seen.Add(expr) || m_keys.KeyOf(expr) is null) return top;
		if (InlineRules.IsMerged(expr)) {
			foreach (var input in expr.Inputs) top = Collect(input, depth, top, nodes, seen);
			return top;
		}
		var size = m_measure.Measure(expr);
		var index = nodes.Count;
		nodes.Add((expr, depth, top));
		var next = top;
		if (expr is not FunctionNode)
			foreach (var input in expr.Inputs) next = Collect(input, depth + 1, next, nodes, seen);
		var used = Math.Max(next, top + size.H + RowGap);
		nodes[index] = (expr, depth, top);
		return used;
	}
}
