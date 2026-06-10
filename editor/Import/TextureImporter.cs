using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using editor.Services;

namespace editor.Import;

public class TextureImporter(TextureImportSettings settings) : IAssetImporter {
	public IReadOnlyList<string> SupportedExtensions => [".png", ".tga"];

	public async Task<IReadOnlyList<string>> Import(string realSourcePath, ImportContext ctx, Action<string> log) {
		var uid = UidGenerator.Generate();
		var name = Path.GetFileNameWithoutExtension(realSourcePath);
		var destPath = Path.Combine(ctx.DestDir, name + ".ktx2");
		Directory.CreateDirectory(ctx.DestDir);

		log("Converting to KTX2...");
		await KtxWriter.ConvertTexture(realSourcePath, destPath, settings, log);

		log("Generating thumbnail...");
		await Task.Run(() => ThumbnailService.Generate(realSourcePath, uid));

		log("Writing .meta sidecar...");
		var header = new MetaHeader { Uid = uid, Type = "texture", Source = ctx.SourceVirtualPath };
		MetaFile.Write(destPath, header, settings.ToSection());

		return [uid];
	}
}
