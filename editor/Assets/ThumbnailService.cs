using System.IO;
using ImageMagick;

namespace editor.Assets;

/// <summary>
///    Generates 110x110 PNG previews into cache://thumbnails/ so the asset browser doesn't have to read the full
///    KTX2
/// </summary>
public static class ThumbnailService {
	private const int Size = 110;

	// Generates a 110×110 PNG thumbnail and saves it to cache://thumbnails/<uid>.png
	public static string Generate(string realSourcePath, string uid) {
		var destDir = Path.Combine(ProjectContext.CachePath, "thumbnails");
		var destPath = Path.Combine(destDir, uid + ".png");
		Directory.CreateDirectory(destDir);

		using var image = new MagickImage(realSourcePath);

		var geo = new MagickGeometry(Size, Size);
		image.Resize(geo);
		image.BackgroundColor = MagickColors.Transparent;
		image.Extent(Size, Size, Gravity.Center);

		image.Write(destPath, MagickFormat.Png);
		return destPath;
	}
}
