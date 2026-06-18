using CommunityToolkit.Mvvm.ComponentModel;

namespace editor.Components.Modals;

public partial class SaveFileViewModel : ObservableObject {
	[ObservableProperty] private string m_path = "assets://untitled.tnode";

	public SaveFileViewModel() { }

	public SaveFileViewModel(string defaultName) {
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
