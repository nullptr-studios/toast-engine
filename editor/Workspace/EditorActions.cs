using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using editor.Engine;

namespace editor.Workspace;

// Inspector buttons marked [[EditorAction("id")]] run a handler registered here instead of calling the engine
public static class EditorActions {
	private static readonly Dictionary<string, Func<HierarchyElement, Task>> s_actions = new();

	public static void Register(string id, Func<HierarchyElement, Task> action) {
		s_actions[id] = action;
	}

	public static void Invoke(string id, HierarchyElement node) {
		if (!s_actions.TryGetValue(id, out var action)) {
			Log.Warn($"No editor action registered for '{id}'");
			return;
		}
		_ = RunAsync(id, action, node);
	}

	private static async Task RunAsync(string id, Func<HierarchyElement, Task> action, HierarchyElement node) {
		try {
			await action(node);
		} catch (Exception ex) {
			Log.Error($"Editor action '{id}' failed: {ex.Message}");
		}
	}
}
