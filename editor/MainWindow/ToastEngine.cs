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

namespace editor.MainWindow;

public class ToastEngine : IDisposable {
	private List<MainWindow> m_toast_windows = [];
	private object m_windows_lock = new object();

	private IntPtr m_engine_handle = IntPtr.Zero;
	private IntPtr m_game_handle = IntPtr.Zero;

	private IntPtr m_engine_instance = IntPtr.Zero;
	private IntPtr m_game_instance = IntPtr.Zero;

	private string path;

	private Task m_tick_task;
	private CancellationTokenSource m_cancellation_source;
	private bool m_close_event_sent;

	public ToastEngine(string toast_path) {
		path = Directory.Exists(toast_path) ? toast_path : Path.GetDirectoryName(toast_path)!;

		prepareLogServer();

		loadToast();
		loadGame();

		// Then we initialize
		m_engine_instance = m_toast_create?.Invoke() ?? IntPtr.Zero;
		m_game_instance = m_game_create?.Invoke() ?? IntPtr.Zero;

		m_uri_set_working_directory?.Invoke(path);
		// m_toast_init?.Invoke();

		// Then we create a window
		createWindow();

		// Start the tick loop on a background thread to avoid blocking the Avalonia render thread
		m_cancellation_source = new CancellationTokenSource();
		m_tick_task = Task.Run(() => tickLoop(m_cancellation_source.Token));
	}

	private void tickLoop(CancellationToken _unused) {
		while (m_toast_should_close() != 1) {
			m_toast_tick();
		}
	}

	public void Dispose() {
		// Wait for the tick loop to finish before destroying
		m_tick_task?.Wait();

		m_game_destroy?.Invoke(m_game_instance);
		m_toast_destroy?.Invoke(m_engine_instance);

		m_cancellation_source?.Dispose();
	}

	public MainWindow createWindow(bool show = true) {
		var w = new MainWindow(this) {
			DataContext = new MainWindowViewModel(this)
		};

		lock (m_windows_lock) {
			m_toast_windows.Add(w);
		}
		if (show) {
			w.Show();
		}

		return w;
	}

	public void removeWindow(MainWindow w) {
		lock (m_windows_lock) {
			m_toast_windows.Remove(w);

			// When all windows are closed, send close event to game engine
			if (m_toast_windows.Count == 0 && !m_close_event_sent) {
				m_close_event_sent = true;
				// TODO: m_toast_close_engine?.Invoke();
			}

		}
	}

	public void reloadGame() {
		m_game_destroy?.Invoke(m_game_instance);
		Thread.Sleep(150);
		loadGame();
	}

	private void loadToast() {
		// Load the Toast Engine DLL
		var app_dir = AppDomain.CurrentDomain.BaseDirectory;
		var engine_dll_path = Path.GetFullPath(Path.Combine(app_dir, "..", "toast_engine", "bin", "toast_engine.dll"));

		if (!File.Exists(engine_dll_path))
			throw new FileNotFoundException($"Engine not found at path {engine_dll_path}");

		m_engine_handle = NativeLibrary.Load(engine_dll_path);

		// Set functions
		m_toast_create =
			Marshal.GetDelegateForFunctionPointer<ToastCreate>(NativeLibrary.GetExport(m_engine_handle,
				"toast_create"));
		m_toast_tick =
			Marshal.GetDelegateForFunctionPointer<ToastTick>(NativeLibrary.GetExport(m_engine_handle, "toast_tick"));
		m_toast_should_close =
			Marshal.GetDelegateForFunctionPointer<ToastShouldClose>(NativeLibrary.GetExport(m_engine_handle,
				"toast_should_close"));
		m_toast_destroy =
			Marshal.GetDelegateForFunctionPointer<ToastDestroy>(NativeLibrary.GetExport(m_engine_handle,
				"toast_destroy"));
		m_uri_set_working_directory =
			Marshal.GetDelegateForFunctionPointer<UriSetWorkingDirectory>(
				NativeLibrary.GetExport(m_engine_handle, "uri_set_working_directory"));
	}

	private void loadGame() {
		var game_dll_path = Directory.EnumerateFiles(Path.Combine(path, "build"), "*.dll").FirstOrDefault();
		if (game_dll_path is null)
			throw new FileNotFoundException($"Game not found at path {path}");

		File.Copy(game_dll_path, Path.Combine(path, ".toast", "game_temp.dll"), true);
		game_dll_path = Path.Combine(path, ".toast", "game_temp.dll"); // update path to new one

		m_game_handle = NativeLibrary.Load(game_dll_path);

		// Set functions
		m_game_create =
			Marshal.GetDelegateForFunctionPointer<GameCreate>(NativeLibrary.GetExport(m_game_handle, "game_create"));
		m_game_destroy =
			Marshal.GetDelegateForFunctionPointer<GameDestroy>(NativeLibrary.GetExport(m_game_handle, "game_destroy"));
	}

	private void prepareLogServer() {
		string app_dir = AppDomain.CurrentDomain.BaseDirectory;
		string exe_extension = OperatingSystem.IsWindows() ? ".exe" : "";
		string binary_name = $"log_server{exe_extension}";

		var target_link_path = Path.Combine(app_dir, binary_name);
		var source_link_path = Path.GetFullPath(Path.Combine(app_dir, "..", "toast_engine", "bin", binary_name));

		if (!File.Exists(target_link_path) && File.Exists(source_link_path)) {
			try {
				File.CreateSymbolicLink(target_link_path, source_link_path);
			}
			catch (UnauthorizedAccessException) {
				// just in case we cannot create a symlink we just copy the executable
				File.Copy(source_link_path, target_link_path, true);
			}
		}
	}

	// TODO: Do wrappers or provide an easy way of calling c++ functions from the dll
	private delegate IntPtr ToastCreate();
	private delegate void ToastTick();
	private delegate int ToastShouldClose();
	private delegate void ToastDestroy(IntPtr engine);
	private delegate void ToastCreateAvaloniaWindow();
	private delegate void UriSetWorkingDirectory([MarshalAs(UnmanagedType.LPStr)] string path);
	private delegate void ToastCloseEngine();

	private delegate IntPtr GameCreate();
	private delegate void GameDestroy(IntPtr game);

	private ToastCreate m_toast_create;
	private ToastTick m_toast_tick;
	private ToastShouldClose m_toast_should_close;
	private ToastDestroy m_toast_destroy;
	private ToastCreateAvaloniaWindow m_toast_create_avalonia;
	private UriSetWorkingDirectory m_uri_set_working_directory;
	private ToastCloseEngine m_toast_close_engine;

	private GameCreate m_game_create;
	private GameDestroy m_game_destroy;
}