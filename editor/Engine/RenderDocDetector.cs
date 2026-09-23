using System;
using System.Diagnostics;

namespace editor.Engine;

/// Detects whether RenderDoc has injected itself into this process (i.e. the editor was launched
/// through RenderDoc, or RenderDoc attached at startup). Checked once, before Avalonia creates any
/// window, so BuildAvaloniaApp() can pick a rendering backend that won't fight RenderDoc's hook
public static class RenderDocDetector {
	public static readonly bool IsAttached = Detect();

	private static bool Detect() {
		try {
			foreach (ProcessModule module in Process.GetCurrentProcess().Modules) {
				if (module.ModuleName.Contains("renderdoc", StringComparison.OrdinalIgnoreCase)) return true;
			}
		} catch {
			// Module enumeration can fail depending on platform/permissions - assume not attached rather
			// than crash editor startup over a diagnostics check
		}

		return false;
	}
}
