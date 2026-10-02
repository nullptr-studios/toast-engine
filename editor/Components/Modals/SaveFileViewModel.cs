using System.IO;
using CommunityToolkit.Mvvm.ComponentModel;

namespace editor.Components.Modals;

public partial class SaveFileViewModel : ObservableObject {
	private string m_extension = ".tnode";
	[ObservableProperty] private string m_folder = "assets://";
	[ObservableProperty] private string m_name = "Untitled";

	public SaveFileViewModel() { }

	public SaveFileViewModel(string defaultName, string extension = ".tnode") {
		var nameExtension = Path.GetExtension(defaultName);
		if (!string.IsNullOrEmpty(nameExtension)) {
			Name = defaultName[..^nameExtension.Length];
			SetExtension(nameExtension);
		} else {
			Name = defaultName;
			SetExtension(extension);
		}
	}

	// updates as user types
	public string FullPath => Result() ?? Folder;

	public void SetExtension(string extension) {
		m_extension = extension.StartsWith('.') ? extension : "." + extension;
	}

	public void SetFolder(string virtualFolder) {
		Folder = virtualFolder;
	}

	partial void OnNameChanged(string value) {
		OnPropertyChanged(nameof(FullPath));
	}

	partial void OnFolderChanged(string value) {
		OnPropertyChanged(nameof(FullPath));
	}

	public string? Result() {
		var trimmedName = Name.Trim();
		if (string.IsNullOrWhiteSpace(trimmedName)) return null;

		var formattedName = trimmedName.Replace(" ", "_");
		var folderPath = Folder.EndsWith('/') ? Folder : Folder + "/";
		var virtualPath = $"{folderPath}{formattedName}{m_extension}";

		return virtualPath;
	}
}
