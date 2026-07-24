using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using editor.Assets.Types;
using Lucide.Avalonia;

namespace editor.Assets.Importers;

public class FontImporter : IAssetImporter {
	public bool CanHandle(string filePath) {
		return Path.GetExtension(filePath) == ".ttf";
	}

	public IReadOnlyList<string> SupportedExtensions => [".ttf"];
	public string DisplayName => "Font";
	public LucideIconKind Icon => LucideIconKind.Type;
	public BaseAsset PrimaryOutputType => AssetTypeRegistry.ByExtension(".ttf")!;

	public IReadOnlyList<ImporterSetting> GetSettings() {
		return [];
	}

	public Task<IReadOnlyList<string>> Import(
		string realSourcePath, ImportContext ctx, Action<string> log, Action<double>? progress = null) {
		try {
			log("Importing font...");
			Directory.CreateDirectory(ctx.DestDir);
			var destPath = Path.Combine(ctx.DestDir, Path.GetFileName(realSourcePath));
			File.Copy(realSourcePath, destPath, true);

			log("Writing .meta sidecar...");
			var uid = ctx.UidFor(0);
			var header = new MetaHeader { Uid = uid, Type = PrimaryOutputType.Type, Source = ctx.SourceVirtualPath };
			MetaFile.Write(destPath, header);
			return Task.FromResult<IReadOnlyList<string>>([uid]);
		} catch (Exception exception) {
			return Task.FromException<IReadOnlyList<string>>(exception);
		}
	}
}
