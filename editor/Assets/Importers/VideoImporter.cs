using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using editor.Assets.Types;
using Lucide.Avalonia;

namespace editor.Assets.Importers;

/// <summary>Copies a video file as is; the engine decodes it at runtime, VP9 .webm plays everywhere.</summary>
public class VideoImporter : IAssetImporter {
	public bool CanHandle(string filePath) {
		return SupportedExtensions.Contains(Path.GetExtension(filePath), StringComparer.OrdinalIgnoreCase);
	}

	public IReadOnlyList<string> SupportedExtensions => [".webm", ".mp4", ".mkv"];
	public string DisplayName => "Video";
	public LucideIconKind Icon => LucideIconKind.Film;
	public BaseAsset PrimaryOutputType => AssetTypeRegistry.ByType("video")!;

	public IReadOnlyList<ImporterSetting> GetSettings() {
		return [];
	}

	public Task<IReadOnlyList<string>> Import(
		string realSourcePath, ImportContext ctx, Action<string> log, Action<double>? progress = null) {
		try {
			log("Importing video...");
			Directory.CreateDirectory(ctx.DestDir);
			var destPath = Path.Combine(ctx.DestDir, Path.GetFileName(realSourcePath));
			File.Copy(realSourcePath, destPath, true);

			log("Writing .meta sidecar...");
			var uid = UidGenerator.Generate();
			var header = new MetaHeader { Uid = uid, Type = PrimaryOutputType.Type, Source = ctx.SourceVirtualPath };
			MetaFile.Write(destPath, header);
			return Task.FromResult<IReadOnlyList<string>>([uid]);
		} catch (Exception exception) {
			return Task.FromException<IReadOnlyList<string>>(exception);
		}
	}
}
