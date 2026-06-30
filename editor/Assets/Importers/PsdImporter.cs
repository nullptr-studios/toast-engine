using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets.Types;
using ImageMagick;
using Lucide.Avalonia;

namespace editor.Assets.Importers;

public enum PsdImportMode { Layers, Folders, Combined }

public partial class PsdImporter : IAssetImporter {
	private readonly Settings m_psdSettings;
	private readonly TextureImporter.Settings m_textureSettings;
	private readonly TextureImporter m_textureImporter;

	public PsdImporter(TextureImporter.Settings textureSettings, Settings psdSettings) {
		m_textureSettings = textureSettings;
		m_psdSettings = psdSettings;
		m_textureImporter = new TextureImporter(textureSettings);
	}

	public IReadOnlyList<string> SupportedExtensions => [".psd"];

	public string DisplayName => "PSD";
	public LucideIconKind Icon => LucideIconKind.Brush;

	public BaseAsset PrimaryOutputType => AssetTypeRegistry.ByExtension(".ktx2")!;

	public IReadOnlyList<IAssetImporter> GetAllSettingsImporters() => [this, m_textureImporter];

	public IReadOnlyList<ImporterSetting> GetSettings() => [
		new ImporterSetting("Create Folder", SettingKind.Bool,
			() => m_psdSettings.CreateFolder,
			v => m_psdSettings.CreateFolder = (bool)v!),
		new ImporterSetting("Import Mode", SettingKind.Enum,
			() => m_psdSettings.ImportMode.ToString(),
			v => m_psdSettings.ImportMode = Enum.Parse<PsdImportMode>((string)v!),
			Options: Enum.GetNames<PsdImportMode>()),
	];

	public async Task<IReadOnlyList<string>> Import(string realSourcePath, ImportContext ctx, Action<string> log,
		Action<double>? progress = null) {
		var baseName = Path.GetFileNameWithoutExtension(realSourcePath);
		var destDir = ctx.DestDir;

		if (m_psdSettings.CreateFolder)
			destDir = Path.Combine(destDir, baseName);

		Directory.CreateDirectory(destDir);
		var outputs = new List<(string tempPng, string destPath, string uid, string layerName)>();
		var tempFiles = new List<string>();

		try {
			log("Reading PSD layers...");
			await Task.Run(() => {
				using var collection = new MagickImageCollection(realSourcePath);
				switch (m_psdSettings.ImportMode) {
					case PsdImportMode.Combined: {
						collection.Merge();
						collection[0].Strip();
						var tempPng = Path.Combine(Path.GetTempPath(), $"{baseName}.png");
						collection[0].Write(tempPng);
						tempFiles.Add(tempPng);
						outputs.Add((tempPng, Path.Combine(destDir, baseName + ".ktx2"),
							ctx.UidFor(outputs.Count), baseName));
						break;
					}
					case PsdImportMode.Layers: {
						for (var i = 0; i < collection.Count; i++) {
							var img = collection[i];
							img.Strip();
							var label = string.IsNullOrWhiteSpace(img.Label) ? $"layer_{i}" : img.Label;
							var safeName = $"{baseName}_{label}";
							var tempPng = Path.Combine(Path.GetTempPath(), safeName + ".png");
							img.Write(tempPng);
							tempFiles.Add(tempPng);
							outputs.Add((tempPng, Path.Combine(destDir, safeName + ".ktx2"),
								ctx.UidFor(outputs.Count), label));
						}

						break;
					}
					case PsdImportMode.Folders: {
						var layersByFolder = new Dictionary<string, List<int>>();
						for (var i = 0; i < collection.Count; i++) {
							var img = collection[i];
							var label = string.IsNullOrWhiteSpace(img.Label) ? $"layer_{i}" : img.Label;
							var folderName = label.Contains('/') ? label[..label.IndexOf('/')] : label;
							if (!layersByFolder.ContainsKey(folderName))
								layersByFolder[folderName] = [];
							layersByFolder[folderName].Add(i);
						}

						foreach (var (folderName, layerIndices) in layersByFolder) {
							using var folderCollection = new MagickImageCollection();
							foreach (var idx in layerIndices)
								folderCollection.Add(collection[idx].Clone());
							folderCollection.Merge();
							folderCollection[0].Strip();
							var tempPng = Path.Combine(Path.GetTempPath(), $"{baseName}_{folderName}.png");
							folderCollection[0].Write(tempPng);
							tempFiles.Add(tempPng);
							outputs.Add((tempPng, Path.Combine(destDir, $"{baseName}_{folderName}.ktx2"),
								ctx.UidFor(outputs.Count), folderName));
						}

						break;
					}
				}
			});

			for (var idx = 0; idx < outputs.Count; idx++) {
				var (tempPng, destPath, uid, _) = outputs[idx];
				progress?.Invoke((double)idx / outputs.Count);

				log("Generating thumbnail...");
				await Task.Run(() => ThumbnailService.Generate(tempPng, uid));

				log($"Converting {Path.GetFileName(destPath)} to KTX2...");
				await KtxWriter.ConvertTexture(tempPng, destPath, m_textureSettings, log);

				log("Writing .meta sidecar...");
				var header = new MetaHeader { Uid = uid, Type = PrimaryOutputType.Type, Source = ctx.SourceVirtualPath };
				MetaFile.Write(destPath, header, m_textureSettings.ToSection(), m_psdSettings.ToSection());
			}
			progress?.Invoke(1.0);
		} finally {
			foreach (var f in tempFiles)
				try {
					File.Delete(f);
				} catch {
					/* ignore */
				}
		}

		return outputs.Select(o => o.uid).ToList();
	}

	public partial class Settings : ObservableObject {
		[ObservableProperty] private bool m_createFolder;
		[ObservableProperty] private PsdImportMode m_importMode = PsdImportMode.Combined;

		public static PsdImportMode[] AllImportModes => Enum.GetValues<PsdImportMode>();

		public PsdMetaSection ToSection() {
			return new PsdMetaSection {
				ImportMode = ImportMode.ToString(),
				CreateFolder = CreateFolder
			};
		}
	}
}
