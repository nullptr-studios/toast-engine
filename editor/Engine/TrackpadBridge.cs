using System;
using System.Runtime.InteropServices;

namespace editor.Engine;

[StructLayout(LayoutKind.Sequential)]
internal struct NativeTrackpadState {
	public float PanX;
	public float PanY;
	public float Zoom;
	public int Inverted;
}

internal static partial class TrackpadBridge {
	internal static bool Supported => NativeSupported() != 0;
	internal static ulong Create(IntPtr nativeWindow) => NativeCreate(nativeWindow);
	internal static void Destroy(ulong handle) => NativeDestroy(handle);

	internal static void SetRect(ulong handle, int x, int y, int width, int height) =>
		NativeSetRect(handle, x, y, width, height);

	internal static void Update(ulong handle) => NativeUpdate(handle);

	internal static bool Drain(ulong handle, out NativeTrackpadState state) => NativeDrain(handle, out state) != 0;
	internal static bool Active(ulong handle) => NativeActive(handle) != 0;

	[LibraryImport("toast_engine", EntryPoint = "toast_trackpad_supported")]
	private static partial int NativeSupported();

	[LibraryImport("toast_engine", EntryPoint = "toast_trackpad_create")]
	private static partial ulong NativeCreate(IntPtr nativeWindow);

	[LibraryImport("toast_engine", EntryPoint = "toast_trackpad_destroy")]
	private static partial void NativeDestroy(ulong handle);

	[LibraryImport("toast_engine", EntryPoint = "toast_trackpad_set_rect")]
	private static partial void NativeSetRect(ulong handle, int x, int y, int width, int height);

	[LibraryImport("toast_engine", EntryPoint = "toast_trackpad_update")]
	private static partial void NativeUpdate(ulong handle);

	[LibraryImport("toast_engine", EntryPoint = "toast_trackpad_drain")]
	private static partial int NativeDrain(ulong handle, out NativeTrackpadState state);

	[LibraryImport("toast_engine", EntryPoint = "toast_trackpad_active")]
	private static partial int NativeActive(ulong handle);
}
