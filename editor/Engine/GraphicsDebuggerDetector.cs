using System;
using System.Diagnostics;

namespace editor.Engine;

public static class GraphicsDebuggerDetector {
	public static readonly bool IsAttached = Detect();

	private static bool Detect() {
		try {
			foreach (ProcessModule module in Process.GetCurrentProcess().Modules) {
				if (IsDebuggerModule(module.ModuleName)) return true;
			}
		} catch {
			// ...
		}

		return false;
	}

	private static bool IsDebuggerModule(string moduleName) {
		if (moduleName.Contains("renderdoc", StringComparison.OrdinalIgnoreCase)) return true;
		return moduleName.Equals("ngfx-capture-injection.dll", StringComparison.OrdinalIgnoreCase) ||
		       moduleName.Equals("Nomad.Injection.dll", StringComparison.OrdinalIgnoreCase) ||
		       moduleName.Equals("Nomad.Injection.Helper.dll", StringComparison.OrdinalIgnoreCase) ||
		       moduleName.Equals("Nvda.Graphics.Interception.dll", StringComparison.OrdinalIgnoreCase);
	}
}
