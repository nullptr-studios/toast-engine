//
// EditorHostWindow.cs by Xein
// 2 Aug 2026
//

using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.VisualTree;
using Dock.Avalonia.Controls;

namespace editor.Workspace;

public class EditorHostWindow : HostWindow {
	public EditorHostWindow() {
		ExtendClientAreaToDecorationsHint = true;
		ExtendClientAreaTitleBarHeightHint = -1;
		WindowDecorations = WindowDecorations.BorderOnly;
	}

	protected override void OnPointerPressed(PointerPressedEventArgs e) {
		base.OnPointerPressed(e);
		if (e.Handled) return;
		if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		if (!IsWindowDragArea(e.Source as Visual)) return;

		e.Handled = true;
		BeginMoveDrag(e);
	}

	private static bool IsWindowDragArea(Visual? source) {
		for (var visual = source; visual is not null; visual = visual.GetVisualParent())
			switch (visual) {
				case Button:
				case ToolTabStripItem:
				case DocumentTabStripItem:
					return false;
				case ToolTabStrip:
				case DocumentTabStrip:
					return true;
			}

		return false;
	}
}
