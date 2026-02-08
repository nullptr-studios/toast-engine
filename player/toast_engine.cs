using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;

namespace player;

public class ToastEngine : IDisposable {
	static ToastEngine() {
		NativeLibrary.SetDllImportResolver(typeof(ToastEngine).Assembly, DllImportResolver);
	}
	
	public ToastEngine() {
		m_handle = toast_create();
		if (m_handle == IntPtr.Zero)
			throw new InvalidOperationException("Failed to create engine");
	}
	
	private static IntPtr DllImportResolver(string libraryName, System.Reflection.Assembly assembly, DllImportSearchPath? searchPath) {
		if (libraryName != "__DYNAMIC_ENGINE__") return IntPtr.Zero;
		string libPath = FindEngineLibrary() ?? throw new Exception("Failed to find toast engine library");
		if (libPath == null)
			throw new FileNotFoundException("Could not find engine library (dll/so/dylib containing 'engine')");
		return NativeLibrary.Load(libPath);
	}
	
	private static string? FindEngineLibrary() {
		string[] extensions;
		if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			extensions = [".dll"];
		else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
			extensions = [".dylib"];
		else
			extensions = [".so"];

		string currentDir = Directory.GetCurrentDirectory();
		foreach (var ext in extensions) {
			var files = Directory.GetFiles(currentDir, $"*engine*{ext}", SearchOption.AllDirectories);
			if (files.Length > 0)
				return files[0];
		}
		
		return null;
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
	[DllImport("__DYNAMIC_ENGINE__", CallingConvention = CallingConvention.Cdecl)]
	private static extern IntPtr toast_create();

	[DllImport("__DYNAMIC_ENGINE__", CallingConvention = CallingConvention.Cdecl)]
	private static extern void toast_tick(IntPtr engine);

	[DllImport("__DYNAMIC_ENGINE__", CallingConvention = CallingConvention.Cdecl)]
	private static extern int toast_should_close(IntPtr engine);

	[DllImport("__DYNAMIC_ENGINE__", CallingConvention = CallingConvention.Cdecl)]
	private static extern void toast_destroy(IntPtr engine);
}
