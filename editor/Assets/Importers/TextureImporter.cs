using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets.Types;
using Lucide.Avalonia;

namespace editor.Assets.Importers;

public enum TextureCompression { None, BC1, BC3, BC4, BC5, BC7, ASTC }

public enum SuperCompression { None, Zstd, BasisLZ }

public enum AddressMode { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder }

public enum FilterMode { Nearest, Linear, Trilinear }

public partial class TextureImporter : IAssetImporter {
	private readonly Settings m_settings;

	public TextureImporter(Settings settings) {
		m_settings = settings;
	}

	public IReadOnlyList<string> SupportedExtensions => [".png", ".jpg", ".jpeg"];

	public bool CanHandle(string filePath) {
		var ext = Path.GetExtension(filePath);
		return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
	}

	public string DisplayName => "Texture";
	public LucideIconKind Icon => LucideIconKind.Image;

	public BaseAsset PrimaryOutputType => AssetTypeRegistry.ByExtension(".ktx2")!;

	public IReadOnlyList<ImporterSetting> GetSettings() {
		return [
			new ImporterSetting("Generate Mipmaps", SettingKind.Bool,
				() => m_settings.GenerateMipmaps,
				v => m_settings.GenerateMipmaps = (bool)v!),
			new ImporterSetting("Compression", SettingKind.Enum,
				() => m_settings.Compression.ToString(),
				v => m_settings.Compression = Enum.Parse<TextureCompression>((string)v!),
				Options: Enum.GetNames<TextureCompression>()),
			new ImporterSetting("Max Resolution", SettingKind.Enum,
				() => m_settings.MaxResolution.ToString(),
				v => m_settings.MaxResolution = int.Parse((string)v!),
				Options: Settings.AllMaxResolutions.Select(r => r.ToString()).ToList()),
			new ImporterSetting("Super Compression", SettingKind.Enum,
				() => m_settings.SuperCompression.ToString(),
				v => m_settings.SuperCompression = Enum.Parse<SuperCompression>((string)v!),
				Options: Enum.GetNames<SuperCompression>()),
			new ImporterSetting("Filter", SettingKind.Enum,
				() => m_settings.Filter.ToString(),
				v => m_settings.Filter = Enum.Parse<FilterMode>((string)v!),
				Options: Enum.GetNames<FilterMode>()),
			new ImporterSetting("Address U", SettingKind.Enum,
				() => m_settings.AddressU.ToString(),
				v => m_settings.AddressU = Enum.Parse<AddressMode>((string)v!),
				Options: Enum.GetNames<AddressMode>()),
			new ImporterSetting("Address V", SettingKind.Enum,
				() => m_settings.AddressV.ToString(),
				v => m_settings.AddressV = Enum.Parse<AddressMode>((string)v!),
				Options: Enum.GetNames<AddressMode>()),
			new ImporterSetting("Anisotropy", SettingKind.Float,
				() => m_settings.Anisotropy,
				v => m_settings.Anisotropy = (float)v!)
		];
	}

	public async Task<IReadOnlyList<string>> Import(
		string realSourcePath, ImportContext ctx, Action<string> log,
		Action<double>? progress = null) {
		var uid = ctx.UidFor(0);
		var name = Path.GetFileNameWithoutExtension(realSourcePath);
		var destPath = Path.Combine(ctx.DestDir, name + ".ktx2");
		Directory.CreateDirectory(ctx.DestDir);

		log("Converting to KTX2...");
		await KtxWriter.ConvertTexture(realSourcePath, destPath, m_settings, log);

		log("Generating thumbnail...");
		await Task.Run(() => ThumbnailService.Generate(realSourcePath, uid));

		log("Writing .meta sidecar...");
		var header = new MetaHeader { Uid = uid, Type = PrimaryOutputType.Type, Source = ctx.SourceVirtualPath };
		MetaFile.Write(destPath, header, m_settings.ToSection());

		progress?.Invoke(1.0);
		return [uid];
	}

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
