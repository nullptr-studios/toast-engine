using System;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Services;

namespace editor.Import;

public enum TextureCompression { None, BC1, BC3, BC4, BC5, BC7, ASTC }

public enum SuperCompression { None, Zstd, BasisLZ }

public enum AddressMode { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder }

public enum FilterMode { Nearest, Linear, Trilinear }

public partial class TextureImportSettings : ObservableObject {
	[ObservableProperty] private AddressMode m_addressU = AddressMode.Repeat;
	[ObservableProperty] private AddressMode m_addressV = AddressMode.Repeat;
	[ObservableProperty] private float m_anisotropy = 8.0f;
	[ObservableProperty] private TextureCompression m_compression = TextureCompression.BC7;
	[ObservableProperty] private FilterMode m_filter = FilterMode.Trilinear;
	[ObservableProperty] private bool m_generateMipmaps = true;
	[ObservableProperty] private int m_maxResolution = 4096;
	[ObservableProperty] private SuperCompression m_superCompression = SuperCompression.Zstd;

	public static TextureCompression[] AllCompressions => Enum.GetValues<TextureCompression>();
	public static SuperCompression[] AllSuperCompressions => Enum.GetValues<SuperCompression>();
	public static AddressMode[] AllAddressModes => Enum.GetValues<AddressMode>();
	public static FilterMode[] AllFilterModes => Enum.GetValues<FilterMode>();
	public static int[] AllMaxResolutions => [64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384];

	public TextureMetaSection ToSection() {
		return new TextureMetaSection {
			GenerateMipmaps = GenerateMipmaps,
			MaxResolution = MaxResolution,
			Compression = Compression.ToString(),
			SuperCompression = SuperCompression.ToString(),
			AddressU = AddressU.ToString(),
			AddressV = AddressV.ToString(),
			Filter = Filter.ToString(),
			Anisotropy = Anisotropy
		};
	}
}

public enum PsdImportMode { Layers, Folders, Combined }

public partial class PsdImportSettings : ObservableObject {
	[ObservableProperty] private bool m_createFolder = false;
	[ObservableProperty] private PsdImportMode m_importMode = PsdImportMode.Combined;

	public static PsdImportMode[] AllImportModes => Enum.GetValues<PsdImportMode>();

	public PsdMetaSection ToSection() {
		return new PsdMetaSection {
			ImportMode = ImportMode.ToString(),
			CreateFolder = CreateFolder
		};
	}
}
