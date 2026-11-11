using Avalonia;
using Avalonia.Input;

namespace editor.Workspace;

internal sealed class HierarchyRowPress<TRow> where TRow : class {
	private const double DragThreshold = 4;
	private (TRow Row, PointerPressedEventArgs Args, Point Start, bool WasSelected)? m_press;

	public void Begin(TRow row, PointerPressedEventArgs args, Visual relativeTo, bool wasSelected) =>
		m_press = (row, args, args.GetPosition(relativeTo), wasSelected);

	public (TRow Row, PointerPressedEventArgs Args)? TakeDrag(PointerEventArgs args, Visual relativeTo) {
		if (m_press is not { } press || !args.GetCurrentPoint(relativeTo).Properties.IsLeftButtonPressed) return null;
		var delta = args.GetPosition(relativeTo) - press.Start;
		if (delta.X * delta.X + delta.Y * delta.Y < DragThreshold * DragThreshold) return null;
		Clear();
		return (press.Row, press.Args);
	}

	public (TRow Row, bool WasSelected)? TakeClick(PointerReleasedEventArgs args) {
		var press = m_press;
		Clear();
		return args.InitialPressMouseButton == MouseButton.Left && press is { } click ? (click.Row, click.WasSelected) : null;
	}

	public void Clear() => m_press = null;
}
