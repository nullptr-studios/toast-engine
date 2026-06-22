using System;

namespace editor.Workspace;

public static class WorkspaceState {
	public static event Action? Modified;

	public static void MarkModified() {
		Modified?.Invoke();
	}
}
