using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using editor.Assets.Types;
using Lucide.Avalonia;

namespace editor.Assets.Importers;

public class UIImageImporter : IAssetImporter {
	public bool CanHandle(string filePath) {
		return Path.GetExtension(filePath) == ".tga";
	}

	public IReadOnlyList<string> SupportedExtensions => [".tga"];
	public string DisplayName => "UI Image";
	public LucideIconKind Icon => LucideIconKind.ImagePlay;
	public BaseAsset PrimaryOutputType => AssetTypeRegistry.ByExtension(".tga")!;

	public IReadOnlyList<ImporterSetting> GetSettings() {
		return [];
	}

	public Task<IReadOnlyList<string>> Import(
		string realSourcePath, ImportContext ctx, Action<string> log, Action<double>? progress = null) {
		try {
			log("Importing UI image...");
			Directory.CreateDirectory(ctx.DestDir);
			var destPath = Path.Combine(ctx.DestDir, Path.GetFileName(realSourcePath));
			File.Copy(realSourcePath, destPath, true);

			log("Writing .meta sidecar...");
			var uid = ctx.UidFor(0);
			var header = new MetaHeader { Uid = uid, Type = PrimaryOutputType.Type, Source = ctx.SourceVirtualPath };
			MetaFile.Write(destPath, header);

			log("Generating thumbnail...");
			ThumbnailService.Generate(destPath, uid);
			return Task.FromResult<IReadOnlyList<string>>([uid]);
		} catch (Exception exception) {
			return Task.FromException<IReadOnlyList<string>>(exception);
		}
	}
}
