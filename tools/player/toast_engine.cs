using System.Runtime.InteropServices;

namespace player;

public class ToastEngine : IDisposable {
	static ToastEngine() {
		NativeResolver.EnsureRegistered();
	}
	
	public ToastEngine() {
		m_handle = toast_create();
		if (m_handle == IntPtr.Zero)
			throw new InvalidOperationException("Failed to create engine");
		toast_create_SDL_window("Hello from C#!!");
	}

	public void Tick() {
		// EnsureNotDisposed();
		toast_tick(m_handle);
	}

	public bool ShouldClose() {
		// EnsureNotDisposed();
		return toast_should_close(m_handle) != 0;
	}

	public void Dispose() {
		if (m_handle != IntPtr.Zero) {
			toast_destroy(m_handle);
			m_handle = IntPtr.Zero;
		}
		GC.SuppressFinalize(this);
	}

	~ToastEngine() {
		Dispose();
	}

	private IntPtr m_handle;

	// Native methods
	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern IntPtr toast_create();

	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern void toast_tick(IntPtr engine);

	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern int toast_should_close(IntPtr engine);

	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern void toast_destroy(IntPtr engine);
	
	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
    private static extern void toast_create_SDL_window(string windowName);
}
