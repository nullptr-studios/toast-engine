//
// SaveWorkspaceViewModel.cs by Xein
//

using CommunityToolkit.Mvvm.ComponentModel;

namespace editor.Workspace;

public partial class SaveWorkspaceViewModel : ObservableObject {
	[ObservableProperty] private string m_path = "assets://untitled.tnode";

	public SaveWorkspaceViewModel() { }

	public SaveWorkspaceViewModel(string defaultName) {
		Path = $"assets://{defaultName}.tnode";
	}

	public void SetFolder(string virtualFolder) {
		var file = Path.Contains('/') ? Path[(Path.LastIndexOf('/') + 1)..] : Path;
		var sep = virtualFolder.EndsWith('/') ? "" : "/";
		Path = $"{virtualFolder}{sep}{file}";
	}

	public string? Result() {
		var p = Path.Trim();
		if (string.IsNullOrWhiteSpace(p)) return null;
		return p.EndsWith(".tnode") ? p : p + ".tnode";
	}
}
