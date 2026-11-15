using System;
using System.Linq;
using editor.LuaEditor.Graph;

namespace editor.LuaEditor.Layout;

public readonly record struct Vec(double X, double Y) {
	public static Vec operator +(Vec a, Vec b) => new(a.X + b.X, a.Y + b.Y);
	public static Vec operator -(Vec a, Vec b) => new(a.X - b.X, a.Y - b.Y);
}

public readonly record struct Size(double W, double H);

public readonly record struct Rect(double X, double Y, double W, double H) {
	public double Right => X + W;
	public double Bottom => Y + H;
	public Vec TopLeft => new(X, Y);

	public bool Intersects(Rect o) => X < o.Right && o.X < Right && Y < o.Bottom && o.Y < Bottom;

	public Rect Inflate(double d) => new(X - d, Y - d, W + 2 * d, H + 2 * d);

	public Rect Offset(Vec v) => new(X + v.X, Y + v.Y, W, H);

	public static Rect Union(Rect a, Rect b) {
		var x = Math.Min(a.X, b.X);
		var y = Math.Min(a.Y, b.Y);
		return new Rect(x, y, Math.Max(a.Right, b.Right) - x, Math.Max(a.Bottom, b.Bottom) - y);
	}
}

public interface INodeMeasure {
	Size Measure(GraphNode node);

	double PinY(GraphNode node, int index, bool output);
}

public sealed class DefaultMeasure : INodeMeasure {
	public const double HeaderHeight = 28;
	public const double RowHeight = 22;
	public const double MinWidth = 120;

	public const double ConstantBoxWidth = 64;

	public static double ConstantWidth(InputPin pin) {
		var choice = InlineRules.EnumValueOf(pin);
		var text = choice ?? pin.Constant?.Raw ?? "nil";
		return Math.Clamp(text.Length * 7 + (choice is not null ? 28 : 20), ConstantBoxWidth, 240);
	}

	private const double LabelChar = 6.6;

	private const double TitleChar = 7.6;

	public static (double Left, double Right) Rails(GraphNode node) {
		var slots = NodeShape.SlotsOf(node);
		double Left(Slot s) => s.Label.Length * LabelChar + 24 + (s.Kind == SlotKind.DataIn && s.In is { } pin && (pin.Source is null || InlineRules.IsMergedName(pin.Source.Owner)) ? ConstantWidth(pin) + 8 : 0);
		var left = slots.Where(s => s.IsLeft).Select(Left).DefaultIfEmpty(0).Max();
		var right = slots.Where(s => !s.IsLeft).Select(s => s.Label.Length * LabelChar + 24).DefaultIfEmpty(0).Max();
		if (node.IsExpanded && NodeShape.CalleeOf(node) is not null) return (Math.Max(left, 96), Math.Max(right, 96));
		return (left, right);
	}

	public Func<FunctionDecl, Size?>? RegionSize { get; set; }

	public static Vec FrameInset(GraphNode host) => new(Rails(host).Left + FramePadX, HeaderHeight + FramePadY);

	public const double FramePadX = 44;

	public const double FramePadY = 40;

	public Size Measure(GraphNode node) {
		if (InlineRules.IsGetterNode(node)) return new Size(NodeShape.TitleOf(node).Length * 7 + 36, GetterHeight);
		var (left, right) = Rails(node);
		var title = NodeShape.TitleOf(node).Length * TitleChar + 44;
		var width = Math.Max(MinWidth, Math.Max(title, left + right + 12));
		if (node is CommentStatement) width = Math.Max(MinWidth, NodeShape.TitleOf(node).Length * 6.8 + 28);
		var height = HeaderHeight + RowHeight * NodeShape.Rows(node) + 8;
		if (NodeShape.ExpansionOf(node) is { } frame) {
			var (innerW, innerH) = (frame.InnerWidth, frame.InnerHeight);
			if (RegionSize?.Invoke(frame.Callee) is { } size) (innerW, innerH) = (Math.Max(size.W, 200), Math.Max(size.H, 110));
			width = left + innerW + right + 2 * FramePadX;
			height = HeaderHeight + Math.Max(RowHeight * NodeShape.Rows(node) + 8, innerH + 2 * FramePadY) + 1;
			return new Size(width, height);
		}
		return new Size(Math.Min(width, 760), height);
	}

	public const double GetterHeight = 22;

	public double PinY(GraphNode node, int row, bool output) => InlineRules.IsGetterNode(node) ? GetterHeight / 2 : HeaderHeight + RowHeight * row + RowHeight / 2;
}
