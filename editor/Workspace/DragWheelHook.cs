using System;
using System.Runtime.InteropServices;

namespace editor.Workspace;

internal sealed class DragWheelHook : IDisposable {
	private const int WhMouseLl = 14;
	private const int WmMouseWheel = 0x020A;
	private const int MouseDataOffset = 8; // MSLLHOOKSTRUCT

	private readonly Action<int> m_onWheel;
	private readonly HookProc m_proc;
	private IntPtr m_hook;

	private DragWheelHook(Action<int> onWheel) {
		m_onWheel = onWheel;
		m_proc = Callback;
	}

	public void Dispose() {
		if (m_hook == IntPtr.Zero) return;
		UnhookWindowsHookEx(m_hook);
		m_hook = IntPtr.Zero;
	}

	public static DragWheelHook? Start(Action<int> onWheel) {
		if (!OperatingSystem.IsWindows()) return null;
		var hook = new DragWheelHook(onWheel);
		hook.m_hook = SetWindowsHookEx(WhMouseLl, hook.m_proc, IntPtr.Zero, 0);
		return hook.m_hook == IntPtr.Zero ? null : hook;
	}

	private IntPtr Callback(int code, IntPtr wParam, IntPtr lParam) {
		if (code >= 0 && wParam.ToInt32() == WmMouseWheel) {
			var data = Marshal.ReadInt32(lParam, MouseDataOffset);
			m_onWheel((short)(data >> 16) / 120);
		}

		return CallNextHookEx(m_hook, code, wParam, lParam);
	}

	private delegate IntPtr HookProc(int code, IntPtr wParam, IntPtr lParam);

	[DllImport("user32.dll", SetLastError = true)]
	private static extern IntPtr SetWindowsHookEx(int idHook, HookProc proc, IntPtr module, uint threadId);

	[DllImport("user32.dll", SetLastError = true)]
	[return: MarshalAs(UnmanagedType.Bool)]
	private static extern bool UnhookWindowsHookEx(IntPtr hook);

	[DllImport("user32.dll")]
	private static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr wParam, IntPtr lParam);
}
