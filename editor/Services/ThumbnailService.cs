using System.IO;
using ImageMagick;

namespace editor.Services;

public static class ThumbnailService {
	private const int Size = 110;

	// Generates a 110×110 PNG thumbnail and saves it to cache://thumbnails/<uid>.png
	public static string Generate(string realSourcePath, string uid) {
		var destDir = Path.Combine(ProjectContext.CachePath, "thumbnails");
		var destPath = Path.Combine(destDir, uid + ".png");
		Directory.CreateDirectory(destDir);

		using var image = new MagickImage(realSourcePath);

		// Fill the 110×110 square while preserving aspect ratio — crops if needed
		var geo = new MagickGeometry(Size, Size);
		image.Resize(geo);
		image.BackgroundColor = MagickColors.Transparent;
		image.Extent(Size, Size, Gravity.Center);

		image.Write(destPath, MagickFormat.Png);
		return destPath;
	}
}
