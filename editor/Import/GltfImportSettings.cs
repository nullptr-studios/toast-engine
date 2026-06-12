using CommunityToolkit.Mvvm.ComponentModel;
using editor.Services;

namespace editor.Import;

public partial class GltfImportSettings : ObservableObject {
	[ObservableProperty] private bool m_createSubfolder = true;

	[ObservableProperty] private bool m_importMaterials = true;
	[ObservableProperty] private bool m_importTextures = true;
	[ObservableProperty] private bool m_importCameras = false;
	[ObservableProperty] private bool m_importLights = true;
	[ObservableProperty] private bool m_generatePrefab = true;

	public GltfMetaSection ToSection() {
		return new GltfMetaSection {
			CreateFolder = CreateSubfolder,
			ImportMaterials = ImportMaterials,
			ImportTextures = ImportTextures,
			ImportCameras = ImportCameras,
			ImportLights = ImportLights,
			GeneratePrefab = GeneratePrefab
		};
	}
}
