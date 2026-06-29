using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using editor.Assets.Types;
using Lucide.Avalonia;

namespace editor.Assets.Importers;

public class AudioBankImporter : IAssetImporter {
	public bool CanHandle(string filePath) => Path.GetExtension(filePath) == ".bank" && Path.GetFileName(filePath) != "Master.strings.bank";
	public IReadOnlyList<string> SupportedExtensions => [".bank"];
	public string DisplayName => "Audio Bank";
	public LucideIconKind Icon => LucideIconKind.AudioWaveform;
	public BaseAsset PrimaryOutputType => AssetTypeRegistry.ByExtension(".bank")!;
	
	public IReadOnlyList<ImporterSetting> GetSettings() => [];
	public Task<IReadOnlyList<string>> Import(string realSourcePath, ImportContext ctx, Action<string> log, Action<double>? progress = null) {
		try {
			log("Importing bank...");
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
