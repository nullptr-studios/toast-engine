using Avalonia;
using Avalonia.VisualTree;

namespace editor.Workspace;

// a view the window level edit shortcuts act through
public interface IWorkspaceShortcutTarget;

public static class ShortcutScope {
	// nothing focused counts as the workspace
	public static bool TargetsWorkspace(object? focused) {
		if (focused is null or Avalonia.Controls.Window) return true;
		if (focused is not Visual start) return false;
		for (var visual = start; visual is not null; visual = visual.GetVisualParent()) {
			if (visual is IWorkspaceShortcutTarget) return true;
		}

		return false;
	}
}
