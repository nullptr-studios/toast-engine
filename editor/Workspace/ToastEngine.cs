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
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using editor.Loader;
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

		// Then we create
		m_engineInstance = toast_create();
		m_gameInstance = m_gameCreate?.Invoke() ?? IntPtr.Zero;

		// we set the working directory
		toast_set_working_directory(
			Path.Combine(ProjectPath, "assets"),
			Path.Combine(ProjectPath, "artworks"),
			Path.Combine(ProjectPath, ".toast"),
			Path.Combine(ProjectPath, ".toast", "saved_data"),
			CorePath
		);

		if (!ProjectContext.IsInitialized)
			ProjectContext.Initialize(ProjectPath, CorePath);

		// and we initialize the engines
		toast_init();

		// Create the engine's render surface
		toast_create_avalonia_window();

		// Then we create a window
		ReflectionDatabase.Update();
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

	private void CreateWorkspace() {
		var desktop = (IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!;
		desktop.MainWindow = new MainWindowView(this) {
			DataContext = new MainWindowViewModel(this)
		};

		desktop.MainWindow.Show();
	}

	public void SignalClose() {
		if (m_closeEventSent) return;
		m_closeEventSent = true;
		Events.Send(new Proto.Events.ExitApplication());
	}

	/// @brief Copies the latest viewport frame into @p dst (capacity bytes)
	/// @return 1 copied, 0 none yet, -1 dst too small
	public int TryGetViewportFrame(IntPtr dst, uint capacity, out ToastViewportFrame frame) {
		return toast_viewport_get_frame(dst, capacity, out frame);
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

		if (!File.Exists(sourceLinkPath)) {
			Console.WriteLine($"Log Server in {sourceLinkPath} not found");
			return;
		}

		try {
			File.CreateSymbolicLink(targetLinkPath, sourceLinkPath);
		} catch (Exception ex) when (ex is UnauthorizedAccessException or IOException) {
			// Creating a symlink needs elevation or Developer Mode on Windows 11
			// Microsoft you little piece of shit this is so retarded im going to kill all your family
			// i cannot even tell you the amount of retardation layers this bug had
			// This MFs failed in coding an OS, a compiler, a debugger and an LSP all in a simple bug
			try {
				File.Copy(sourceLinkPath, targetLinkPath, true);
			} catch (IOException) {
				Console.WriteLine("Couldn't copy log_server.exe to editor dir, using previous version");
			}
		}
	}

	// --------------------- Engine C ABI shit ---------------------
	[LibraryImport(EngineLib)]
	private static partial IntPtr toast_create();

	[LibraryImport(EngineLib)]
	private static partial IntPtr toast_init();

	[LibraryImport(EngineLib)]
	private static partial void toast_tick();

	[LibraryImport(EngineLib)]
	private static partial int toast_should_close();

	[LibraryImport(EngineLib)]
	private static partial void toast_destroy(IntPtr engine);

	[LibraryImport(EngineLib)]
	private static partial void toast_create_avalonia_window();

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_set_working_directory(string assets, string artworks, string cache, string saved, string core);

	[LibraryImport(EngineLib)]
	private static partial int toast_viewport_get_frame(IntPtr dst, uint dstCapacity, out ToastViewportFrame outFrame);

	// --------------------- Game ABI shit ---------------------
	private delegate IntPtr GameCreate();

	private delegate void GameDestroy(IntPtr game);
}
