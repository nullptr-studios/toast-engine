using System.Reflection;
using System.Runtime.InteropServices;

namespace player;

internal static class NativeResolver {
	private static readonly object s_lock = new();
	private static bool s_registered;

	// PInvoke dlopen to allow RTLD_GLOBAL preload of engine library on Linux
	[DllImport("libdl")]
	private static extern IntPtr dlopen(string filename, int flags);

	internal static void EnsureRegistered() {
		lock (s_lock) {
			if (s_registered) return;
			NativeLibrary.SetDllImportResolver(typeof(NativeResolver).Assembly, Resolve);
			s_registered = true;
		}
	}

	private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath) {
		var pattern = libraryName switch {
			"__ENGINE_LIB__" => "*engine*",
			"__APPLICATION_LIB__" => "*game*",
			_ => null!
		};
		if (pattern == null) return IntPtr.Zero;

		var isLinux = RuntimeInformation.IsOSPlatform(OSPlatform.Linux);

		// Search roots
		var roots = new List<string>();
		try {
			var baseDir = AppContext.BaseDirectory;
			if (!string.IsNullOrEmpty(baseDir)) roots.Add(baseDir);
		} catch (Exception ex) {
			Console.WriteLine($"[NativeResolver] baseDir error: {ex.Message}");
		}

		roots.Add(Directory.GetCurrentDirectory());

		string[] extensions = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? [".dll"]
			: RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? [".dylib"]
			: [".so"];

		// If resolving the application library, attempt to preload the engine library with RTLD_GLOBAL so DT_NEEDED is resolved
		if (libraryName == "__APPLICATION_LIB__" && isLinux)
			foreach (var ext in extensions)
			foreach (var root in roots)
				try {
					var engineFiles = Directory.GetFiles(root, "*engine*" + ext, SearchOption.AllDirectories);
					if (engineFiles.Length > 0) {
						try {
							const int RTLD_NOW = 2;
							const int RTLD_GLOBAL = 0x100;
							var h = dlopen(engineFiles[0], RTLD_NOW | RTLD_GLOBAL);
							if (h == IntPtr.Zero) Console.WriteLine("[NativeResolver] dlopen returned NULL");
						} catch (Exception ex) {
							Console.WriteLine($"[NativeResolver] dlopen failed: {ex.Message}");
						}

						goto LoadApp;
					}
				} catch (Exception ex) {
					Console.WriteLine($"[NativeResolver] search error: {ex.Message}");
				}

		LoadApp:
		foreach (var ext in extensions)
		foreach (var root in roots)
			try {
				var files = Directory.GetFiles(root, pattern + ext, SearchOption.AllDirectories);
				if (libraryName == "__APPLICATION_LIB__") {
					var preferred = files.FirstOrDefault(path =>
						Path.GetFileNameWithoutExtension(path).Equals("my_game", StringComparison.OrdinalIgnoreCase));
					if (preferred is not null) {
						files = [preferred];
					} else {
						var projectGames = files.Where(path =>
							!Path.GetFileNameWithoutExtension(path).Equals("dummy_game", StringComparison.OrdinalIgnoreCase)
						).ToArray();
						if (projectGames.Length > 0) files = projectGames;
					}
				}
				if (files.Length <= 0) continue;
				try {
					// Change CWD to the library's dir so '.' in RUNPATH resolves correctly
					var libDir = Path.GetDirectoryName(files[0]);
					var oldCwd = Directory.GetCurrentDirectory();
					try {
						if (!string.IsNullOrEmpty(libDir)) Directory.SetCurrentDirectory(libDir);
						if (!isLinux) return NativeLibrary.Load(files[0]);
						// Try dlopen directly to ensure DT_NEEDED deps are resolved via RTLD_GLOBAL
						const int RTLD_NOW = 2;
						const int RTLD_GLOBAL = 0x100;
						var handle = dlopen(files[0], RTLD_NOW | RTLD_GLOBAL);
						if (handle != IntPtr.Zero)
							return handle;

						return NativeLibrary.Load(files[0]);
					} finally {
						try {
							Directory.SetCurrentDirectory(oldCwd);
						} catch { }
					}
				} catch (Exception ex) {
					Console.WriteLine($"[NativeResolver] Load failed: {ex.Message}");
				}
			} catch (Exception ex) {
				Console.WriteLine($"[NativeResolver] search error: {ex.Message}");
			}

		// Give up
		Console.WriteLine($"[NativeResolver] Could not find native library for '{libraryName}'");
		return IntPtr.Zero;
	}
}
