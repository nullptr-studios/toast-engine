using System.Runtime.InteropServices;

namespace player;

public class ApplicationLayer : IDisposable {
	private IntPtr m_handle;

	static ApplicationLayer() {
		NativeResolver.EnsureRegistered();
	}

	public ApplicationLayer() {
		m_handle = game_create();
		if (m_handle == IntPtr.Zero)
			throw new InvalidOperationException("Failed to create application layer");
	}

	public void Dispose() {
		if (m_handle != IntPtr.Zero)
			// toast_destroy(m_handle);
			m_handle = IntPtr.Zero;
		GC.SuppressFinalize(this);
	}

	~ApplicationLayer() {
		Dispose();
	}

	// Native methods
	[DllImport("__APPLICATION_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern IntPtr game_create();
}
