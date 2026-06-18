using System;

namespace editor.Workspace;

/// <summary>Fires when the editor makes a change, before the engine confirms it.</summary>
public static class WorkspaceState {
	public static event Action? Modified;

	public static void MarkModified() {
		Modified?.Invoke();
	}
}
