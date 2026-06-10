//
// ToastEngine.cs by Xein
// 2 Jun 2026
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Dock.Model.Controls;
using editor.Services;

namespace editor.Workspace;

/// @brief Description of the latest rendered viewport frame
[StructLayout(LayoutKind.Sequential)]
public struct ToastViewportFrame {
	public uint width;
	public uint height;
	public uint row_pitch;
	public ulong frame_id;
}

public partial class ToastEngine : IDisposable {
	private const string EngineLib = "toast_engine";
	private readonly CancellationTokenSource m_cancellationSource;
	private bool m_closeEventSent;

	private readonly IntPtr m_engineInstance;

	private GameCreate? m_gameCreate;
	private GameDestroy? m_gameDestroy;

	private IntPtr m_gameHandle = IntPtr.Zero;
	private readonly IntPtr m_gameInstance;

	private readonly Task m_tickTask;

	private readonly List<Workspace> m_toastWindows = [];
	private readonly Lock m_windowsLock = new();

	// The engine DLL lives next to the executable at ../toast_engine/bin
	// We need to specify its location before the application runs
	static ToastEngine() {
		NativeLibrary.SetDllImportResolver(typeof(ToastEngine).Assembly, (name, _, _) => {
			if (name != EngineLib) return IntPtr.Zero;
			return NativeLibrary.Load(EngineDllPath());
		});
	}

	public ToastEngine(string toastPath) {
		ProjectPath = Directory.Exists(toastPath) ? toastPath : Path.GetDirectoryName(toastPath)!;
		var dll = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "toast_engine", "bin"));
		CorePath = Path.Combine(dll, "assets");

		PrepareLogServer();

		// Load the engine DLL into the process FIRST
		// the game DLL depends on toast_engine.dll, and the OS loader resolves that dependency by base name
		// against already-loaded modules
		NativeLibrary.Load(EngineDllPath());

		LoadGame();

		// Then we initialize
		m_engineInstance = toast_create();
		m_gameInstance = m_gameCreate?.Invoke() ?? IntPtr.Zero;

		toast_set_working_directory(ProjectPath, dll);

		if (!ProjectContext.IsInitialized)
			ProjectContext.Initialize(ProjectPath, CorePath);

		// Create the engine's render surface
		toast_create_avalonia_window();

		// Then we create a window
		CreateWorkspace();

		// Start the tick loop on a background thread to avoid blocking the Avalonia render thread
		m_cancellationSource = new CancellationTokenSource();
		m_tickTask = Task.Run(() => TickLoop(m_cancellationSource.Token));
	}

	public string ProjectPath { get; }

	public string CorePath { get; }

	public void Dispose() {
		// Wait for the tick loop to finish before destroying
		m_tickTask.Wait();

		m_gameDestroy?.Invoke(m_gameInstance);
		toast_destroy(m_engineInstance);

		m_cancellationSource.Dispose();
	}

	private static string EngineDllPath() {
		var dll = Path.GetFullPath(
			Path.Combine(AppContext.BaseDirectory, "..", "toast_engine", "bin", "toast_engine.dll"));
		if (!File.Exists(dll))
			throw new FileNotFoundException($"Engine not found at path {dll}");
		return dll;
	}

	private void TickLoop(CancellationToken unused) {
		while (toast_should_close() != 1) toast_tick();
	}

	public Workspace CreateWorkspace(bool show = true, IRootDock? layout = null) {
		var w = new Workspace(this) {
			DataContext = new WorkspaceViewModel(this, layout)
		};

		lock (m_windowsLock) {
			m_toastWindows.Add(w);
		}

		if (show) w.Show();

		return w;
	}

	public void RemoveWorkspace(Workspace w) {
		lock (m_windowsLock) {
			m_toastWindows.Remove(w);

			// When all windows are closed, send close event to game engine
			if (m_toastWindows.Count == 0 && !m_closeEventSent) m_closeEventSent = true;
			// TODO: m_toast_close_engine?.Invoke();
		}
	}

	public void SignalClose() {
		if (!m_closeEventSent) m_closeEventSent = true;
		// TODO: m_toast_close_engine?.Invoke();
	}

	/// @brief Copies the latest viewport frame into @p dst (capacity bytes)
	/// @return 1 copied, 0 none yet, -1 dst too small
	public int TryGetViewportFrame(IntPtr dst, uint capacity, out ToastViewportFrame frame) {
		return toast_viewport_get_frame(dst, capacity, out frame);
	}

	public void SendMousePosition(float x, float y) {
		toast_send_mouse_position(x, y);
	}

	public void SendMouseButton(int button, int action, int mods) {
		toast_send_mouse_button(button, action, mods);
	}

	public void SendMouseScroll(float x, float y) {
		toast_send_mouse_scroll(x, y);
	}

	public void SendKey(int key, int scancode, int action, int mods) {
		toast_send_key(key, scancode, action, mods);
	}

	public void SendChar(uint codepoint) {
		toast_send_char(codepoint);
	}

	public void SendResize(int width, int height) {
		toast_send_resize(width, height);
	}

	public void ReloadGame() {
		m_gameDestroy?.Invoke(m_gameInstance);
		Thread.Sleep(150);
		LoadGame();
	}

	private void LoadGame() {
		var gameDllPath = Directory.EnumerateFiles(Path.Combine(ProjectPath, "build"), "*.dll").FirstOrDefault();
		if (gameDllPath is null)
			throw new FileNotFoundException($"Game not found at path {ProjectPath}");

		File.Copy(gameDllPath, Path.Combine(ProjectPath, ".toast", "game_temp.dll"), true);
		gameDllPath = Path.Combine(ProjectPath, ".toast", "game_temp.dll"); // update path to new one

		m_gameHandle = NativeLibrary.Load(gameDllPath);

		// Set functions
		m_gameCreate =
			Marshal.GetDelegateForFunctionPointer<GameCreate>(NativeLibrary.GetExport(m_gameHandle, "game_create"));
		m_gameDestroy =
			Marshal.GetDelegateForFunctionPointer<GameDestroy>(NativeLibrary.GetExport(m_gameHandle, "game_destroy"));
	}

	private void PrepareLogServer() {
		var appDir = AppDomain.CurrentDomain.BaseDirectory;
		var exeExtension = OperatingSystem.IsWindows() ? ".exe" : "";
		var binaryName = $"log_server{exeExtension}";

		var targetLinkPath = Path.Combine(appDir, binaryName);
		var sourceLinkPath = Path.GetFullPath(Path.Combine(appDir, "..", "toast_engine", "bin", binaryName));

		if (!File.Exists(targetLinkPath) && File.Exists(sourceLinkPath))
			try {
				File.CreateSymbolicLink(targetLinkPath, sourceLinkPath);
			} catch (UnauthorizedAccessException) {
				// just in case we cannot create a symlink we just copy the executable
				File.Copy(sourceLinkPath, targetLinkPath, true);
			}
	}

	// --------------------- Engine C ABI shit ---------------------
	[LibraryImport(EngineLib)]
	private static partial IntPtr toast_create();

	[LibraryImport(EngineLib)]
	private static partial void toast_tick();

	[LibraryImport(EngineLib)]
	private static partial int toast_should_close();

	[LibraryImport(EngineLib)]
	private static partial void toast_destroy(IntPtr engine);

	[LibraryImport(EngineLib)]
	private static partial void toast_create_avalonia_window();

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_set_working_directory(string project, string engine);

	[LibraryImport(EngineLib)]
	private static partial int toast_viewport_get_frame(IntPtr dst, uint dstCapacity, out ToastViewportFrame outFrame);

	[LibraryImport(EngineLib)]
	private static partial void toast_send_mouse_position(float x, float y);

	[LibraryImport(EngineLib)]
	private static partial void toast_send_mouse_button(int button, int action, int mods);

	[LibraryImport(EngineLib)]
	private static partial void toast_send_mouse_scroll(float x, float y);

	[LibraryImport(EngineLib)]
	private static partial void toast_send_key(int key, int scancode, int action, int mods);

	[LibraryImport(EngineLib)]
	private static partial void toast_send_char(uint codepoint);

	[LibraryImport(EngineLib)]
	private static partial void toast_send_resize(int width, int height);

	// --------------------- Game ABI shit ---------------------
	private delegate IntPtr GameCreate();

	private delegate void GameDestroy(IntPtr game);
}
