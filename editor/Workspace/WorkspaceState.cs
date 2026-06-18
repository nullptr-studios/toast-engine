using System;

namespace editor.Workspace;

// This is optimistic
// it fires when the editor *requests* a change, not when the engine confirms one
public static class WorkspaceState {
	public static event Action? Modified;

	public static void MarkModified() => Modified?.Invoke();
}
