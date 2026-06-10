using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using ImageMagick;
using editor.Services;

namespace editor.Import;

public class PsdImporter(TextureImportSettings textureSettings, PsdImportSettings psdSettings)
	: IAssetImporter {
	public IReadOnlyList<string> SupportedExtensions => [".psd"];

	public async Task<IReadOnlyList<string>> Import(string realSourcePath, ImportContext ctx, Action<string> log) {
		var baseName = Path.GetFileNameWithoutExtension(realSourcePath);
		var destDir = ctx.DestDir;

		if (psdSettings.CreateFolder) {
			destDir = Path.Combine(destDir, baseName);
		}

		Directory.CreateDirectory(destDir);
		var outputs = new List<(string tempPng, string destPath, string uid, string layerName)>();
		var tempFiles = new List<string>();

		try {
			log("Reading PSD layers...");
			await Task.Run(() => {
				using var collection = new MagickImageCollection(realSourcePath);
				switch (psdSettings.ImportMode) {
					case PsdImportMode.Combined: {
						collection.Merge();
						collection[0].Strip();
						var tempPng = Path.Combine(Path.GetTempPath(), $"{baseName}.png");
						collection[0].Write(tempPng);
						tempFiles.Add(tempPng);
						outputs.Add((tempPng, Path.Combine(destDir, baseName + ".ktx2"),
							UidGenerator.Generate(), baseName));
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
								UidGenerator.Generate(), label));
						}

						break;
					}
					case PsdImportMode.Folders: {
						var layersByFolder = new Dictionary<string, List<int>>();
						for (var i = 0; i < collection.Count; i++) {
							var img = collection[i];
							var label = string.IsNullOrWhiteSpace(img.Label) ? $"layer_{i}" : img.Label;
							var folderName = label.Contains("/") ? label[..label.IndexOf("/")] : label;
							if (!layersByFolder.ContainsKey(folderName))
								layersByFolder[folderName] = [];
							layersByFolder[folderName].Add(i);
						}

						foreach (var (folderName, layerIndices) in layersByFolder) {
							using var folderCollection = new MagickImageCollection();
							foreach (var idx in layerIndices) {
								folderCollection.Add(collection[idx].Clone());
							}
							folderCollection.Merge();
							folderCollection[0].Strip();
							var tempPng = Path.Combine(Path.GetTempPath(), $"{baseName}_{folderName}.png");
							folderCollection[0].Write(tempPng);
							tempFiles.Add(tempPng);
							outputs.Add((tempPng, Path.Combine(destDir, $"{baseName}_{folderName}.ktx2"),
								UidGenerator.Generate(), folderName));
						}

						break;
					}
				}
			});

			foreach (var (tempPng, destPath, uid, _) in outputs) {
				log("Generating thumbnail...");
				await Task.Run(() => ThumbnailService.Generate(tempPng, uid));

				log($"Converting {Path.GetFileName(destPath)} to KTX2...");
				await KtxWriter.ConvertTexture(tempPng, destPath, textureSettings, log);

				log("Writing .meta sidecar...");
				var header = new MetaHeader { Uid = uid, Type = "texture", Source = ctx.SourceVirtualPath };
				MetaFile.Write(destPath, header, textureSettings.ToSection(), psdSettings.ToSection());
			}
		} finally {
			foreach (var f in tempFiles) {
				try { File.Delete(f); } catch { /* ignore */ }
			}
		}

		return outputs.Select(o => o.uid).ToList();
	}
}
