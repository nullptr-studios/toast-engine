using System;
using System.Runtime.ExceptionServices;
using System.Threading;

namespace editor.LuaEditor.Graph;

// long expression chains can overflow the default stack
internal static class StackGuard {
	private const int StackBytes = 256 * 1024 * 1024;

	[ThreadStatic]
	private static bool s_inside;

	public static T Run<T>(Func<T> work) {
		if (s_inside) return work();

		T? result = default;
		ExceptionDispatchInfo? failure = null;
		var thread = new Thread(() => {
			s_inside = true;
			try {
				result = work();
			} catch (Exception e) {
				failure = ExceptionDispatchInfo.Capture(e);
			}
		}, StackBytes);
		thread.Start();
		thread.Join();
		failure?.Throw();
		return result!;
	}
}
