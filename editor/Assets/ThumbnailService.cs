using System;
using System.IO;
using System.Runtime.InteropServices;
using ImageMagick;

namespace editor.Assets;

/// <summary>
///    Generates 110x110 PNG previews into cache://thumbnails
/// </summary>
public static partial class ThumbnailService {
	private const int Size = 110;

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

	public static string GenerateFromKtx2(string ktx2Path, string uid) {
		var destDir = Path.Combine(ProjectContext.CachePath, "thumbnails");
		var destPath = Path.Combine(destDir, uid + ".png");
		Directory.CreateDirectory(destDir);

		var pixels = new byte[Size * Size * 4];
		if (toast_ktx2_decode_thumbnail(ktx2Path, pixels, Size) == 0)
			throw new Exception($"Native KTX2 thumbnail decode failed for '{ktx2Path}'");

		var settings = new PixelReadSettings(Size, Size, StorageType.Char, PixelMapping.RGBA);
		using var image = new MagickImage(pixels, settings);
		image.Write(destPath, MagickFormat.Png);
		return destPath;
	}

	public static string GenerateFromVoxel(string tvoxPath, string palettePath, string uid) {
		var destDir = Path.Combine(ProjectContext.CachePath, "thumbnails");
		var destPath = Path.Combine(destDir, uid + ".png");
		Directory.CreateDirectory(destDir);

		var pixels = new byte[Size * Size * 4];
		if (toast_tvox_render_thumbnail(tvoxPath, palettePath ?? "", pixels, Size) == 0)
			throw new Exception($"Native voxel thumbnail render failed for '{tvoxPath}'");

		var settings = new PixelReadSettings(Size, Size, StorageType.Char, PixelMapping.RGBA);
		using var image = new MagickImage(pixels, settings);
		image.Write(destPath, MagickFormat.Png);
		return destPath;
	}

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial int toast_ktx2_decode_thumbnail(string path, byte[] dst, uint thumbSize);

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial int toast_tvox_render_thumbnail(string tvoxPath, string palettePath, byte[] dst, uint thumbSize);
}
