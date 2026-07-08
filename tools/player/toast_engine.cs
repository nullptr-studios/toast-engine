using System.Runtime.InteropServices;

namespace player;

public class ToastEngine : IDisposable {
	private IntPtr m_handle;

	static ToastEngine() {
		NativeResolver.EnsureRegistered();
	}

	public ToastEngine() {
		m_handle = toast_create();
		if (m_handle == IntPtr.Zero)
			throw new InvalidOperationException("Failed to create engine");
	}

	public void Dispose() {
		if (m_handle != IntPtr.Zero) {
			toast_destroy(m_handle);
			m_handle = IntPtr.Zero;
		}

		GC.SuppressFinalize(this);
	}

	public void Init() {
		toast_init();
	}

	public void Tick() {
		toast_tick(m_handle);
	}

	public bool ShouldClose() {
		return toast_should_close(m_handle) != 0;
	}

	public void CreateSdlWindow(string windowName) {
		toast_create_SDL_window(windowName);
	}

	public void SetWorkingDirectory(string project, string artworks, string cache, string saved, string core) {
		toast_set_working_directory(project, artworks, cache, saved, core);
	}

	public void SetLoadMode(bool gameMode) {
		toast_set_load_mode(gameMode ? 1 : 0);
	}

	public void MountPack(string scheme, string pakPath) {
		toast_mount_pack(scheme, pakPath);
	}

	public void StartGame() {
		toast_start_game();
	}

	~ToastEngine() {
		Dispose();
	}

	// Native methods
	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern IntPtr toast_create();

	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern void toast_init();

	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern void toast_tick(IntPtr engine);

	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern int toast_should_close(IntPtr engine);

	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern void toast_destroy(IntPtr engine);

	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern void toast_create_SDL_window(string windowName);

	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern void toast_set_working_directory(
		string project, string artworks, string cache, string saved, string core);

	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern void toast_set_load_mode(int mode);

	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern void toast_mount_pack(string scheme, string pakPath);

	[DllImport("__ENGINE_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern void toast_start_game();
}
