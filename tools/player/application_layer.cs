using System.Runtime.InteropServices;

namespace player;

public class ApplicationLayer : IDisposable {
	static ApplicationLayer() {
		NativeResolver.EnsureRegistered();
	}

	public ApplicationLayer() {
		m_handle = game_create();
		if (m_handle == IntPtr.Zero)
			throw new InvalidOperationException("Failed to create application layer");
	}

	public void Dispose() {
		if (m_handle != IntPtr.Zero) {
			m_handle = IntPtr.Zero;
		}
		GC.SuppressFinalize(this);
	}

	~ApplicationLayer() {
		Dispose();
	}

	private IntPtr m_handle;

	// Native methods
	[DllImport("__APPLICATION_LIB__", CallingConvention = CallingConvention.Cdecl)]
	private static extern IntPtr game_create();
}
