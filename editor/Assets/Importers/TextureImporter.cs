using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;

namespace editor.Assets.Importers;

public enum TextureCompression { None, BC1, BC3, BC4, BC5, BC7, ASTC }

public enum SuperCompression { None, Zstd, BasisLZ }

public enum AddressMode { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder }

public enum FilterMode { Nearest, Linear, Trilinear }

/// <summary>PNG/TGA → KTX2 via toktx.</summary>
public partial class TextureImporter : IAssetImporter {
	private readonly Settings m_settings;

	public TextureImporter(Settings settings) {
		m_settings = settings;
	}

	public IReadOnlyList<string> SupportedExtensions => [".png", ".tga"];

	public async Task<IReadOnlyList<string>> Import(string realSourcePath, ImportContext ctx, Action<string> log) {
		var uid = ctx.UidFor(0);
		var name = Path.GetFileNameWithoutExtension(realSourcePath);
		var destPath = Path.Combine(ctx.DestDir, name + ".ktx2");
		Directory.CreateDirectory(ctx.DestDir);

		log("Converting to KTX2...");
		await KtxWriter.ConvertTexture(realSourcePath, destPath, m_settings, log);

		log("Generating thumbnail...");
		await Task.Run(() => ThumbnailService.Generate(realSourcePath, uid));

		log("Writing .meta sidecar...");
		var header = new MetaHeader { Uid = uid, Type = "texture", Source = ctx.SourceVirtualPath };
		MetaFile.Write(destPath, header, m_settings.ToSection());

		return [uid];
	}

	/// <summary>Import settings, bound to the import window.</summary>
	public partial class Settings : ObservableObject {
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
}
