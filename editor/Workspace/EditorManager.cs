using System;
using editor.Assets;

namespace editor.Workspace;

public static class EditorManager {
    public static event Action<AssetFile>? OpenRequested;

    public static void RequestOpen(AssetFile file) => OpenRequested?.Invoke(file);
}
