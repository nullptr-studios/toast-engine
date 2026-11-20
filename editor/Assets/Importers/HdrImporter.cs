using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using editor.Assets.Types;
using Lucide.Avalonia;

namespace editor.Assets.Importers;

public class HdrImporter : IAssetImporter {
	public bool CanHandle(string filePath) {
		return Path.GetExtension(filePath).Equals(".hdr", StringComparison.OrdinalIgnoreCase);
	}

	public IReadOnlyList<string> SupportedExtensions => [".hdr"];
	public string DisplayName => "Environment Map";
	public LucideIconKind Icon => LucideIconKind.Sun;
	public BaseAsset PrimaryOutputType => AssetTypeRegistry.ByExtension(".hdr")!;

	public IReadOnlyList<ImporterSetting> GetSettings() {
		return [];
	}

	public Task<IReadOnlyList<string>> Import(
		string realSourcePath, ImportContext ctx, Action<string> log, Action<double>? progress = null) {
		try {
			log("Importing environment map...");
			Directory.CreateDirectory(ctx.DestDir);
			var destPath = Path.Combine(ctx.DestDir, Path.GetFileName(realSourcePath));
			File.Copy(realSourcePath, destPath, true);

			var uid = UidGenerator.Generate();
			log("Generating thumbnail...");
			ThumbnailService.GenerateFromHdr(destPath, uid);

			log("Writing .meta sidecar...");
			var header = new MetaHeader { Uid = uid, Type = PrimaryOutputType.Type, Source = ctx.SourceVirtualPath };
			MetaFile.Write(destPath, header);
			progress?.Invoke(1.0);
			return Task.FromResult<IReadOnlyList<string>>([uid]);
		} catch (Exception exception) {
			return Task.FromException<IReadOnlyList<string>>(exception);
		}
	}
}
