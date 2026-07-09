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
	private IntPtr m_currentGameInstance;

	private readonly Task m_tickTask;

	private readonly ManualResetEventSlim m_tickGate = new(initialState: true);
	private readonly ManualResetEventSlim m_tickIdle = new(initialState: true);

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

		var gameDll = FindGameDll();
		var tempPath = GameDllTempPath;
		Directory.CreateDirectory(Path.GetDirectoryName(tempPath)!);
		File.Copy(gameDll, tempPath, overwrite: true);
		LoadFrom(tempPath);

		m_engineInstance = toast_create();
		m_currentGameInstance = m_gameCreate?.Invoke() ?? IntPtr.Zero;

		toast_set_working_directory(
			ProjectPath,
			Path.Combine(ProjectPath, "artworks"),
			Path.Combine(ProjectPath, ".toast"),
			Path.Combine(ProjectPath, ".toast", "saved_data"),
			CorePath
		);

		if (!ProjectContext.IsInitialized)
			ProjectContext.Initialize(ProjectPath, CorePath);

		// init after set_working_directory so the engine knows where to find its assets
		toast_init();
		IsEngineReady = true;
		toast_create_avalonia_window();

		ReflectionDatabase.Update();
		CreateMainWindow();

		// tick loop runs on a background thread and just calls toast_tick() in a tight loop
		// until the engine signals it wants to close
		m_cancellationSource = new CancellationTokenSource();
		m_tickTask = Task.Run(() => TickLoop(m_cancellationSource.Token));
	}

	public static bool IsEngineReady { get; private set; }

	public string ProjectPath { get; }
	public string CorePath { get; }

	private static string NativeLibDir => OperatingSystem.IsWindows() ? "bin" : "lib";
	private static string NativeLibPrefix => OperatingSystem.IsWindows() ? "" : "lib";
	private static string NativeLibExt => OperatingSystem.IsWindows() ? ".dll" : ".so";

	private string GameDllTempPath => Path.Combine(ProjectPath, ".toast", $"game_temp{NativeLibExt}");

	public void Dispose() {
		IsEngineReady = false;
		m_cancellationSource.Cancel();
		m_tickTask.Wait();
		m_gameDestroy?.Invoke(m_currentGameInstance);
		toast_destroy(m_engineInstance);
		m_cancellationSource.Dispose();
	}

	public WorkspaceResult CreateWorkspace(string type) {
		return toast_create_workspace(type);
	}

	public WorkspaceResult OpenWorkspace(string assetUid) {
		return toast_open_workspace(assetUid);
	}

	/// Opens a workspace bound to assetUid but loading its content from an autosave
	public WorkspaceResult OpenWorkspaceFrom(string assetUid, string sourceUri) {
		return toast_open_workspace_from(assetUid, sourceUri);
	}

	/// Clones the given workspace's live tree into a new ticking PlayWorkspace
	public WorkspaceResult PlayWorkspace(ulong sourceHandle) {
		return toast_play_workspace(sourceHandle);
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
		// Pause tick loop and wait for frame to finish
		m_tickGate.Reset();
		m_tickIdle.Wait();

		Events.Send(new SetFocusedNode { Node = "" });

		try {
			// Overwrite the temp copy of the game DLL
			if (File.Exists(GameDllTempPath))
				File.Delete(GameDllTempPath);
			File.Copy(FindGameDll(), GameDllTempPath);

			// Load the NEW library while the OLD one is still loaded
			// the old .dll stays valid until dlclose
			// this ensures we never have a gap where no game library is available
			var newHandle = NativeLibrary.Load(GameDllTempPath);
			var newCreate =
				Marshal.GetDelegateForFunctionPointer<GameCreate>(NativeLibrary.GetExport(newHandle, "game_create"));
			var newDestroy =
				Marshal.GetDelegateForFunctionPointer<GameDestroy>(NativeLibrary.GetExport(newHandle, "game_destroy"));

			toast_pop_application();
			m_currentGameInstance = IntPtr.Zero;

			if (m_gameHandle != IntPtr.Zero)
				NativeLibrary.Free(m_gameHandle);

			m_gameHandle = newHandle;
			m_gameCreate = newCreate;
			m_gameDestroy = newDestroy;

			m_currentGameInstance = m_gameCreate?.Invoke() ?? IntPtr.Zero;
			toast_begin_application();
		} catch (Exception ex) {
			Console.Error.WriteLine($"Hot reload failed: {ex.Message}");
		} finally {
			m_tickGate.Set();
		}
	}

	private static string EngineDllPath() {
		var name = $"{NativeLibPrefix}toast_engine{NativeLibExt}";
		var path = Path.GetFullPath(
			Path.Combine(AppContext.BaseDirectory, "..", "toast_engine", NativeLibDir, name));
		if (!File.Exists(path))
			throw new FileNotFoundException($"Engine not found at path {path}");
		return path;
	}

	private void TickLoop(CancellationToken token) {
		while (!token.IsCancellationRequested && toast_should_close() != 1) {
			// Wait until the gate is open
			m_tickGate.Wait(token);
			if (token.IsCancellationRequested) break;

			m_tickIdle.Reset();
			try {
				toast_tick();
			} finally {
				m_tickIdle.Set();
			}
		}
	}

	private void CreateMainWindow() {
		var desktop = (IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!;
		desktop.MainWindow = new MainWindowView(this) {
			DataContext = new MainWindowViewModel(this)
		};
		desktop.MainWindow.Show();
	}

	private string FindGameDll() {
		var path = Directory.EnumerateFiles(Path.Combine(ProjectPath, "build"), $"*{NativeLibExt}")
			.FirstOrDefault(f => Path.GetFileName(f).Contains("game", StringComparison.OrdinalIgnoreCase));
		if (path is null)
			throw new FileNotFoundException($"Game DLL not found in {Path.Combine(ProjectPath, "build")}");
		return path;
	}

	private void LoadFrom(string dllPath) {
		m_gameHandle = NativeLibrary.Load(dllPath);
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
		string project, string artworks, string cache, string saved, string core);

	[LibraryImport(EngineLib)]
	private static partial int toast_viewport_get_frame(IntPtr dst, uint dstCapacity, out ToastViewportFrame outFrame);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial WorkspaceResult toast_create_workspace(string type);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial WorkspaceResult toast_open_workspace(string uid);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial WorkspaceResult toast_open_workspace_from(string uid, string sourceUri);

	[LibraryImport(EngineLib)]
	private static partial WorkspaceResult toast_play_workspace(ulong sourceHandle);

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_rename_prefab_root(string path, string newName);

	public static void RenamePrefabRoot(string path, string newName) {
		toast_rename_prefab_root(path, newName);
	}

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_create_tnode(string path, string nodeType);

	public static void CreateTNode(string path, string nodeType) {
		toast_create_tnode(path, nodeType);
	}

	[LibraryImport(EngineLib)]
	private static partial void toast_reload_manifest();

	public static void ReloadManifest() {
		toast_reload_manifest();
	}

	[LibraryImport(EngineLib)]
	private static partial void toast_reload_project_settings();

	public static void ReloadProjectSettings() {
		if (IsEngineReady) toast_reload_project_settings();
	}

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_haptics_test(string tomlText);

	/// Plays a haptic described by .thaptic TOML text on the active controller
	public static void TestHaptic(string tomlText) {
		toast_haptics_test(tomlText);
	}

	[LibraryImport(EngineLib)]
	private static partial void toast_begin_application();

	[LibraryImport(EngineLib)]
	private static partial void toast_pop_application();

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial void toast_bake_asset(string uid, string outPath);

	public static void BakeAsset(string uid, string outPath) {
		if (IsEngineReady) toast_bake_asset(uid, outPath);
	}

	private delegate IntPtr GameCreate();

	private delegate void GameDestroy(IntPtr game);
}
