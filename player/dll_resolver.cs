using System.Runtime.InteropServices;

namespace player;

internal static class NativeResolver {
    private static readonly object s_lock = new();
    private static bool s_registered = false;

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

    private static IntPtr Resolve(string libraryName, System.Reflection.Assembly assembly, DllImportSearchPath? searchPath) {
        string pattern = libraryName switch {
            "__ENGINE_LIB__" => "*engine*",
            "__APPLICATION_LIB__" => "*game*",
            _ => null!
        };
        if (pattern == null) return IntPtr.Zero;

        // System.Console.WriteLine($"[NativeResolver] Resolve called for: {libraryName}");

        // Search roots: assembly location, AppContext.BaseDirectory, current directory
        var roots = new System.Collections.Generic.List<string>();
        try {
            var asmDir = System.IO.Path.GetDirectoryName(assembly.Location);
            if (!string.IsNullOrEmpty(asmDir)) roots.Add(asmDir);
        } catch (Exception ex) { System.Console.WriteLine($"[NativeResolver] asm location error: {ex.Message}"); }
        try {
            var baseDir = AppContext.BaseDirectory;
            if (!string.IsNullOrEmpty(baseDir)) roots.Add(baseDir);
        } catch (Exception ex) { System.Console.WriteLine($"[NativeResolver] baseDir error: {ex.Message}"); }
        roots.Add(Directory.GetCurrentDirectory());

        // System.Console.WriteLine("[NativeResolver] search roots:");
        // foreach (var r in roots) System.Console.WriteLine("  " + r);

        string[] extensions = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? new[] { ".dll" }
            : RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? new[] { ".dylib" }
            : new[] { ".so" };

        // If resolving the application library, attempt to preload the engine library with RTLD_GLOBAL so DT_NEEDED is resolved
        if (libraryName == "__APPLICATION_LIB__") {
            foreach (var ext in extensions) {
                foreach (var root in roots) {
                    try {
                        var engineFiles = Directory.GetFiles(root, "*engine*" + ext, SearchOption.AllDirectories);
                        if (engineFiles.Length > 0) {
                            // System.Console.WriteLine($"[NativeResolver] Preloading engine via dlopen: {engineFiles[0]}");
                            try {
                                const int RTLD_NOW = 2;
                                const int RTLD_GLOBAL = 0x100;
                                var h = dlopen(engineFiles[0], RTLD_NOW | RTLD_GLOBAL);
                                if (h == IntPtr.Zero) System.Console.WriteLine("[NativeResolver] dlopen returned NULL"); else System.Console.WriteLine("[NativeResolver] dlopen succeeded");
                            } catch (Exception ex) { System.Console.WriteLine($"[NativeResolver] dlopen failed: {ex.Message}"); }
                            goto LoadApp;
                        }
                    } catch (Exception ex) { System.Console.WriteLine($"[NativeResolver] search error: {ex.Message}"); }
                }
            }
        }

    LoadApp:
        foreach (var ext in extensions) {
            foreach (var root in roots) {
                try {
                    var files = Directory.GetFiles(root, pattern + ext, SearchOption.AllDirectories);
                    if (files.Length > 0) {
                        // System.Console.WriteLine($"[NativeResolver] Loading application lib from: {files[0]}");
                        try {
                            // Change CWD to the library's dir so '.' in RUNPATH resolves correctly
                            var libDir = System.IO.Path.GetDirectoryName(files[0]);
                            var oldCwd = Directory.GetCurrentDirectory();
                            try {
                                if (!string.IsNullOrEmpty(libDir)) Directory.SetCurrentDirectory(libDir);
                                // Try dlopen directly to ensure DT_NEEDED deps are resolved via RTLD_GLOBAL
                                const int RTLD_NOW = 2;
                                const int RTLD_GLOBAL = 0x100;
                                var handle = dlopen(files[0], RTLD_NOW | RTLD_GLOBAL);
                                if (handle != IntPtr.Zero) {
                                    // System.Console.WriteLine("[NativeResolver] dlopen(app) succeeded");
                                    return handle;
                                }
                                // System.Console.WriteLine("[NativeResolver] dlopen(app) returned NULL, falling back to NativeLibrary.Load");
                                return NativeLibrary.Load(files[0]);
                            } finally {
                                try { Directory.SetCurrentDirectory(oldCwd); } catch { }
                            }
                        } catch (Exception ex) { System.Console.WriteLine($"[NativeResolver] Load failed: {ex.Message}"); }
                    }
                } catch (Exception ex) { System.Console.WriteLine($"[NativeResolver] search error: {ex.Message}"); }
            }
        }

        // Give up
        System.Console.WriteLine($"[NativeResolver] Could not find native library for '{libraryName}'");
        return IntPtr.Zero;
    }
}
