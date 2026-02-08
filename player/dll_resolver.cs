using System.Runtime.InteropServices;

namespace player;

internal static class NativeResolver {
    private static readonly object s_lock = new();
    private static bool s_registered = false;

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

        string currentDir = Directory.GetCurrentDirectory();
        string[] extensions = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? [".dll"]
            : RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? [".dylib"]
            : [".so"];
        foreach (var ext in extensions) {
            var files = Directory.GetFiles(currentDir, pattern + ext, SearchOption.AllDirectories);
            if (files.Length > 0)
                return NativeLibrary.Load(files[0]);
        }
        throw new FileNotFoundException($"Could not find native library for '{libraryName}'");
    }
}
