using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using editor.Assets;
using editor.Workspace;
using Proto.Events;

namespace editor.Engine;

/// Description of the latest rendered viewport frame
[StructLayout(LayoutKind.Sequential)]
public struct ToastViewportFrame {
	public uint width;
	public uint height;
	public uint row_pitch;
	public ulong frame_id;
}

// returned by create/open workspace calls
// Uid == 0 means it failed, Name points into engine memory
[StructLayout(LayoutKind.Sequential)]
public struct WorkspaceResult {
	public ulong Uid;
	public nint Name;
}

public partial class ToastEngine : IDisposable {
	private const string EngineLib = "toast_engine";
	private readonly CancellationTokenSource m_cancellationSource;

	private readonly IntPtr m_engineInstance;
	private readonly IntPtr m_gameInstance;

	private readonly Task m_tickTask;

	private readonly Lock m_windowsLock = new();
	private bool m_closeEventSent;

	private GameCreate? m_gameCreate;
	private GameDestroy? m_gameDestroy;

	private IntPtr m_gameHandle = IntPtr.Zero;

	// the engine dll lives at ../toast_engine/bin
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

		// engine first -> game dll links against it and the OS loader resolves
		// that dep by matching base names of already-loaded modules
		NativeLibrary.Load(EngineDllPath());

		LoadGame();

		m_engineInstance = toast_create();
		m_gameInstance = m_gameCreate?.Invoke() ?? IntPtr.Zero;

		toast_set_working_directory(
			Path.Combine(ProjectPath, "assets"),
			Path.Combine(ProjectPath, "artworks"),
			Path.Combine(ProjectPath, ".toast"),
			Path.Combine(ProjectPath, ".toast", "saved_data"),
			CorePath
		);

		if (!ProjectContext.IsInitialized)
			ProjectContext.Initialize(ProjectPath, CorePath);

		// init after set_working_directory so the engine knows where to find its assets
		toast_init();
		toast_create_avalonia_window();

		ReflectionDatabase.Update();
		CreateMainWindow();

		// tick loop runs on a background thread and just calls toast_tick() in a tight loop
		// until the engine signals it wants to close
		m_cancellationSource = new CancellationTokenSource();
		m_tickTask = Task.Run(() => TickLoop(m_cancellationSource.Token));
	}

	public string ProjectPath { get; }
	public string CorePath { get; }

	public void Dispose() {
		m_cancellationSource.Cancel();
		m_tickTask.Wait();
		m_gameDestroy?.Invoke(m_gameInstance);
		toast_destroy(m_engineInstance);
		m_cancellationSource.Dispose();
	}

	public WorkspaceResult CreateWorkspace(string type) {
		return toast_create_workspace(type);
	}

	public WorkspaceResult OpenWorkspace(string assetUid) {
		return toast_open_workspace(assetUid);
	}

	// copies the latest rendered frame into dst (capacity bytes)
	// returns 1 = frame copied, 0 = no frame yet, -1 = dst too small
	public int TryGetViewportFrame(IntPtr dst, uint capacity, out ToastViewportFrame frame) {
		return toast_viewport_get_frame(dst, capacity, out frame);
	}

	// guards against sending close twice
	public void SignalClose() {
		if (m_closeEventSent) return;
		m_closeEventSent = true;
		Events.Send(new ExitApplication());
	}

	public void ReloadGame() {
		m_gameDestroy?.Invoke(m_gameInstance);
		Thread.Sleep(150); // give the OS time to release handles before loading a new copy
		LoadGame();
	}

	private static string NativeLibDir    => OperatingSystem.IsWindows() ? "bin" : "lib";
	private static string NativeLibPrefix => OperatingSystem.IsWindows() ? ""    : "lib";
	private static string NativeLibExt    => OperatingSystem.IsWindows() ? ".dll" : ".so";

	private static string EngineDllPath() {
		var name = $"{NativeLibPrefix}toast_engine{NativeLibExt}";
		var path = Path.GetFullPath(
			Path.Combine(AppContext.BaseDirectory, "..", "toast_engine", NativeLibDir, name));
		if (!File.Exists(path))
			throw new FileNotFoundException($"Engine not found at path {path}");
		return path;
	}

	private void TickLoop(CancellationToken token) {
		while (!token.IsCancellationRequested && toast_should_close() != 1) toast_tick();
	}

	private void CreateMainWindow() {
		var desktop = (IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!;
		desktop.MainWindow = new MainWindowView(this) {
			DataContext = new MainWindowViewModel(this)
		};
		desktop.MainWindow.Show();
	}

	private void LoadGame() {
		var gameDllPath = Directory.EnumerateFiles(Path.Combine(ProjectPath, "build"), $"*{NativeLibExt}").FirstOrDefault();
		if (gameDllPath is null)
			throw new FileNotFoundException($"Game not found at path {ProjectPath}");

		// copy to a temp path so the original stays unlocked and the build system
		// can overwrite it while the editor is running without us holding the file
		var tempFile = $"game_temp{NativeLibExt}";
		File.Copy(gameDllPath, Path.Combine(ProjectPath, ".toast", tempFile), true);
		gameDllPath = Path.Combine(ProjectPath, ".toast", tempFile);

		m_gameHandle = NativeLibrary.Load(gameDllPath);

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
			Log.Warn($"Log Server in {sourceLinkPath} not found");
			return;
		}

		try {
			if (File.Exists(targetLinkPath)) File.Delete(targetLinkPath);
			File.CreateSymbolicLink(targetLinkPath, sourceLinkPath);
		} catch (Exception ex) when (ex is UnauthorizedAccessException or IOException) {
			// Creating a symlink needs elevation or Developer Mode on Windows 11
			// Microsoft you little piece of shit this is so retarded im going to kill all your family
			// i cannot even tell you the amount of retardation layers this bug had
			// This MFs failed in coding an OS, a compiler, a debugger and an LSP all in a simple bug
			try {
				File.Copy(sourceLinkPath, targetLinkPath, true);
			} catch (IOException) {
				Log.Error($"Couldn't copy {binaryName} to editor dir, using previous version");
			}
		}
	}

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
	private static partial void toast_set_working_directory(
		string assets, string artworks, string cache, string saved, string core);

	[LibraryImport(EngineLib)]
	private static partial int toast_viewport_get_frame(IntPtr dst, uint dstCapacity, out ToastViewportFrame outFrame);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial WorkspaceResult toast_create_workspace(string type);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial WorkspaceResult toast_open_workspace(string uid);

	private delegate IntPtr GameCreate();

	private delegate void GameDestroy(IntPtr game);
}
