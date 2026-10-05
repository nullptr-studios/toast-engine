using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using editor.Assets;
using editor.StartWindow;
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

/// A workspace the engine created; Uid == 0 means it failed
public readonly record struct WorkspaceInfo(ulong Uid, string? Name);

public partial class ToastEngine : IDisposable {
	private const string EngineLib = "toast_engine";
	private readonly CancellationTokenSource m_cancellationSource;

	private readonly IntPtr m_engineInstance;
	private readonly List<(IntPtr Handle, string Path)> m_retiredGameLibraries = [];

	private readonly ManualResetEventSlim m_tickGate = new(true);
	private readonly ManualResetEventSlim m_tickIdle = new(true);

	// Work the engine only accepts from the thread that ticks it, see OnTickThread
	private readonly ConcurrentQueue<TickWork> m_tickQueue = new();
	private static ToastEngine? s_instance;
	private volatile bool m_ticking;
	private volatile int m_tickThreadId;

	private readonly Task m_tickTask;

	//private readonly Lock m_windowsLock = new();
	private bool m_closeEventSent;
	private IntPtr m_currentGameInstance;

	private GameCreate? m_gameCreate;
	private GameDestroy? m_gameDestroy;

	private IntPtr m_gameHandle = IntPtr.Zero;
	private string? m_gameTempPath;

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
		var tempPath = CreateGameTempPath();
		Directory.CreateDirectory(Path.GetDirectoryName(tempPath)!);
		File.Copy(gameDll, tempPath, true);
		LoadFrom(tempPath);

		m_engineInstance = toast_create();
		Log.Info($"Loading game library: {gameDll}");
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
		DataSchemaStubGenerator.Generate();
		toast_create_avalonia_window();

		ReflectionDatabase.Update();
		CreateMainWindow();

		// tick loop runs on a background thread and just calls toast_tick() in a tight loop
		// until the engine signals it wants to close
		s_instance = this;
		m_cancellationSource = new CancellationTokenSource();
		m_tickTask = Task.Run(() => TickLoop(m_cancellationSource.Token));
	}

	public static bool IsEngineReady { get; private set; }

	/// Unfocused holds the renderer at 30 fps, minimized stops rendering until the editor is restored
	public static void SetWindowState(bool focused, bool minimized) {
		if (!IsEngineReady) return;
		toast_set_window_state(focused ? 1 : 0, minimized ? 1 : 0);
	}

	public string ProjectPath { get; }
	public string CorePath { get; }

	private static string NativeLibDir => OperatingSystem.IsWindows() ? "bin" : "lib";
	private static string NativeLibPrefix => OperatingSystem.IsWindows() ? "" : "lib";
	private static string NativeLibExt => OperatingSystem.IsWindows() ? ".dll" : ".so";

	public void Dispose() {
		IsEngineReady = false;
		m_cancellationSource.Cancel();
		m_tickTask.Wait();
		if (ReferenceEquals(s_instance, this)) s_instance = null;
		m_gameDestroy?.Invoke(m_currentGameInstance);
		toast_destroy(m_engineInstance);
		ReleaseGameLibraries();
		m_cancellationSource.Dispose();

		UpdateProjectListOnClose();
	}

	private string CreateGameTempPath() {
		return Path.Combine(
			ProjectPath,
			".toast",
			$"game_temp_{Environment.ProcessId}_{Guid.NewGuid():N}{NativeLibExt}"
		);
	}

	private void UpdateProjectListOnClose() {
		var toastFile = Directory.EnumerateFiles(ProjectPath, "*.toast").FirstOrDefault();
		if (toastFile is null) return;

		var projectList = ProjectList.LoadList();
		projectList.Upsert(toastFile);
		projectList.SaveList();
	}

	// Building a workspace instantiates nodes and runs their Lua scripts, so it happens on the tick thread. The name
	// the engine returns points into thread local storage there, so it is copied out before the work item ends
	private static WorkspaceInfo ToInfo(WorkspaceResult result) {
		return new WorkspaceInfo(result.Uid, Marshal.PtrToStringUTF8(result.Name));
	}

	public WorkspaceInfo CreateWorkspace(string type) {
		return OnTickThread(() => ToInfo(toast_create_workspace(type)));
	}

	public WorkspaceInfo OpenWorkspace(string assetUid) {
		return OnTickThread(() => ToInfo(toast_open_workspace(assetUid)));
	}

	/// Opens a workspace bound to assetUid but loading its content from an autosave
	public WorkspaceInfo OpenWorkspaceFrom(string assetUid, string sourceUri) {
		return OnTickThread(() => ToInfo(toast_open_workspace_from(assetUid, sourceUri)));
	}

	/// Clones the given workspace's live tree into a new ticking PlayWorkspace
	public WorkspaceInfo PlayWorkspace(ulong sourceHandle) {
		return OnTickThread(() => ToInfo(toast_play_workspace(sourceHandle)));
	}

	/// The engine's node trees and Lua interpreters belong to the thread that ticks it. Anything that builds or rebuilds
	/// them (opening a workspace, playing, reloading the manifest) runs there, because doing it from the UI thread while
	/// a tick is running races with the scripts being executed. Blocks the caller until the work finished
	public static T OnTickThread<T>(Func<T> work) {
		var engine = s_instance;
		if (engine is null || !engine.m_ticking || Environment.CurrentManagedThreadId == engine.m_tickThreadId) return work();

		var result = default(T)!;
		Exception? failure = null;
		using var finished = new ManualResetEventSlim(false);
		engine.m_tickQueue.Enqueue(new TickWork(
			() => {
				try {
					result = work();
				} catch (Exception ex) {
					failure = ex;
				} finally {
					finished.Set();
				}
			},
			() => {
				failure = new OperationCanceledException("The engine stopped before the work could run");
				finished.Set();
			}));
		// The loop may have ended right before the enqueue, and then nobody is left to run it
		if (!engine.m_ticking) engine.CancelPendingTickWork();
		finished.Wait();
		if (failure is not null) ExceptionDispatchInfo.Capture(failure).Throw();
		return result;
	}

	public static void OnTickThread(Action work) {
		OnTickThread(() => {
			work();
			return true;
		});
	}

	private sealed record TickWork(Action Run, Action Cancel);

	private void DrainTickQueue() {
		while (m_tickQueue.TryDequeue(out var work)) work.Run();
	}

	private void CancelPendingTickWork() {
		while (m_tickQueue.TryDequeue(out var work)) work.Cancel();
	}

	// copies the latest rendered frame into dst (capacity bytes)
	// returns 1 = frame copied, 0 = no frame yet, -1 = dst too small
	public int TryGetViewportFrame(IntPtr dst, uint capacity, out ToastViewportFrame frame) {
		frame = default;
		// Guard against late UI-thread ticks calling into a freed engine during shutdown
		if (!IsEngineReady) return 0;
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

		var newHandle = IntPtr.Zero;
		var adopted = false;
		var newTempPath = CreateGameTempPath();
		try {
			var gameDll = FindGameDll();
			Log.Info($"Reloading game library: {gameDll}");
			File.Copy(gameDll, newTempPath);
			newHandle = NativeLibrary.Load(newTempPath);
			var newCreate =
				Marshal.GetDelegateForFunctionPointer<GameCreate>(NativeLibrary.GetExport(newHandle, "game_create"));
			var newDestroy =
				Marshal.GetDelegateForFunctionPointer<GameDestroy>(NativeLibrary.GetExport(newHandle, "game_destroy"));

			toast_pop_application();
			m_currentGameInstance = IntPtr.Zero;

			if (m_gameHandle != IntPtr.Zero && m_gameTempPath is not null)
				m_retiredGameLibraries.Add((m_gameHandle, m_gameTempPath));

			m_gameHandle = newHandle;
			m_gameTempPath = newTempPath;
			m_gameCreate = newCreate;
			m_gameDestroy = newDestroy;
			adopted = true;

			m_currentGameInstance = m_gameCreate?.Invoke() ?? IntPtr.Zero;
			toast_begin_application();
		} catch (Exception ex) {
			if (!adopted) {
				if (newHandle != IntPtr.Zero) NativeLibrary.Free(newHandle);
				TryDeleteGameTemp(newTempPath);
			}

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
		m_tickThreadId = Environment.CurrentManagedThreadId;
		m_ticking = true;
		try {
			while (!token.IsCancellationRequested && toast_should_close() != 1) {
				// Wait until the gate is open
				m_tickGate.Wait(token);
				if (token.IsCancellationRequested) break;

				m_tickIdle.Reset();
				try {
					// Work the UI asked for runs between two ticks, never inside one
					DrainTickQueue();
					toast_tick();
				} finally {
					m_tickIdle.Set();
				}
			}
		} catch (OperationCanceledException) {
			// Dispose() cancels the token while the loop is on m_tickGate
		} finally {
			m_ticking = false;
			CancelPendingTickWork();
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
		var buildDirectory = Path.Combine(ProjectPath, "build");
		var libraries = Directory.EnumerateFiles(buildDirectory, $"*{NativeLibExt}").ToArray();
		var preferred = libraries.FirstOrDefault(path =>
			Path.GetFileNameWithoutExtension(path).Equals("my_game", StringComparison.OrdinalIgnoreCase));
		if (preferred is not null) return preferred;

		var candidates = libraries.Where(path => {
			var name = Path.GetFileNameWithoutExtension(path);
			return name.Contains("game", StringComparison.OrdinalIgnoreCase) &&
				!name.Equals("dummy_game", StringComparison.OrdinalIgnoreCase);
		}).ToArray();

		return candidates.Length switch {
			1 => candidates[0],
			0 => throw new FileNotFoundException($"Game DLL not found in {buildDirectory}"),
			_ => throw new InvalidOperationException(
				$"Multiple game DLLs found in {buildDirectory}: {string.Join(", ", candidates.Select(Path.GetFileName))}"
			)
		};
	}

	private void LoadFrom(string dllPath) {
		m_gameHandle = NativeLibrary.Load(dllPath);
		m_gameTempPath = dllPath;
		m_gameCreate =
			Marshal.GetDelegateForFunctionPointer<GameCreate>(NativeLibrary.GetExport(m_gameHandle, "game_create"));
		m_gameDestroy =
			Marshal.GetDelegateForFunctionPointer<GameDestroy>(NativeLibrary.GetExport(m_gameHandle, "game_destroy"));
	}

	private void ReleaseGameLibraries() {
		if (m_gameHandle != IntPtr.Zero) {
			NativeLibrary.Free(m_gameHandle);
			m_gameHandle = IntPtr.Zero;
		}

		if (m_gameTempPath is not null) TryDeleteGameTemp(m_gameTempPath);

		foreach (var (handle, path) in m_retiredGameLibraries) {
			NativeLibrary.Free(handle);
			TryDeleteGameTemp(path);
		}

		m_retiredGameLibraries.Clear();
	}

	private static void TryDeleteGameTemp(string path) {
		try {
			if (File.Exists(path)) File.Delete(path);
		} catch (IOException) {
			// Native loader teardown can briefly retain a mapped shadow copy
		} catch (UnauthorizedAccessException) { }
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
	private static partial void toast_init();

	[LibraryImport(EngineLib)]
	private static partial void toast_tick();

	[LibraryImport(EngineLib)]
	private static partial int toast_should_close();

	[LibraryImport(EngineLib)]
	private static partial void toast_destroy(IntPtr engine);

	[LibraryImport(EngineLib)]
	private static partial void toast_create_avalonia_window();

	[LibraryImport(EngineLib)]
	private static partial void toast_set_window_state(int focused, int minimized);

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
		OnTickThread(() => toast_create_tnode(path, nodeType));
	}

	[LibraryImport(EngineLib)]
	private static partial void toast_reload_manifest();

	public static void ReloadManifest() {
		OnTickThread(toast_reload_manifest);
	}

	[LibraryImport(EngineLib)]
	private static partial void toast_reload_project_settings();

	public static void ReloadProjectSettings() {
		if (IsEngineReady) OnTickThread(toast_reload_project_settings);
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

	[LibraryImport(EngineLib, StringMarshalling = StringMarshalling.Utf8)]
	private static partial int toast_generate_data_schema_stubs(string outPath);

	/// Regenerates the Schemas.* lua stuff
	public static bool GenerateDataSchemaStubs(string outPath) {
		return IsEngineReady && toast_generate_data_schema_stubs(outPath) != 0;
	}

	private delegate IntPtr GameCreate();

	private delegate void GameDestroy(IntPtr game);
}
