using CommunityToolkit.Mvvm.ComponentModel;

namespace editor.Components.Modals;

public partial class SaveFileViewModel : ObservableObject {
	[ObservableProperty] private string m_name = "Untitled";
	[ObservableProperty] private string m_folder = "assets://";
	private string m_extension = ".tnode";

	public SaveFileViewModel() { }

	public SaveFileViewModel(string defaultName, string extension = ".tnode") {
		Name = defaultName;
		m_extension = extension;
	}

	public void SetExtension(string extension) {
		m_extension = extension.StartsWith('.') ? extension : "." + extension;
	}

	public void SetFolder(string virtualFolder) {
		Folder = virtualFolder;
	}

	// updates as user types
	public string FullPath => Result() ?? Folder;

	partial void OnNameChanged(string value) => OnPropertyChanged(nameof(FullPath));
	partial void OnFolderChanged(string value) => OnPropertyChanged(nameof(FullPath));

	public string? Result() {
		var trimmedName = Name.Trim();
		if (string.IsNullOrWhiteSpace(trimmedName)) return null;

		var formattedName = trimmedName.Replace(" ", "_");
		var folderPath = Folder.EndsWith('/') ? Folder : Folder + "/";
		var virtualPath = $"{folderPath}{formattedName}{m_extension}";

		return virtualPath;
	}
}
