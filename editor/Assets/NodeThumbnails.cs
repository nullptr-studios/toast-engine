using System;
using System.IO;
using editor.Engine;
using ImageMagick;

namespace editor.Assets;

public static class NodeThumbnails {
	private const int Width = 122;
	private const int Height = 110;

	private static byte[] s_buffer = [];

	public static event Action<string>? Updated;

	// Copies the latest viewport frame
	public static void Capture(ToastEngine? engine, string? uid) {
		if (engine is null || string.IsNullOrEmpty(uid)) return;

		var result = engine.TryGetViewportFrame(IntPtr.Zero, 0, out var frame);
		if (result == -1) {
			var bytes = (int)(frame.row_pitch * frame.height);
			if (s_buffer.Length < bytes) s_buffer = new byte[bytes];
			unsafe {
				fixed (byte* dst = s_buffer) result = engine.TryGetViewportFrame((IntPtr)dst, (uint)s_buffer.Length, out frame);
			}
		}
		if (result != 1 || frame.width == 0 || frame.height == 0) return;

		try {
			Write(uid, s_buffer, (int)frame.width, (int)frame.height, (int)frame.row_pitch);
			Updated?.Invoke(uid);
		} catch (Exception e) {
			Log.Warn($"Could not write the thumbnail of {uid}: {e.Message}");
		}
	}

	private static void Write(string uid, byte[] bgra, int width, int height, int pitch) {
		var destDir = Path.Combine(ProjectContext.CachePath, "thumbnails");
		Directory.CreateDirectory(destDir);

		var packed = new byte[width * height * 4];
		for (var y = 0; y < height; y++) Buffer.BlockCopy(bgra, y * pitch, packed, y * width * 4, width * 4);

		var settings = new PixelReadSettings((uint)width, (uint)height, StorageType.Char, PixelMapping.BGRA);
		using var image = new MagickImage(packed, settings);
		image.Alpha(AlphaOption.Off);
		// The largest middle rectangle with the card aspect
		var cropW = Math.Min(width, height * Width / Height);
		var cropH = Math.Min(height, width * Height / Width);
		image.Crop(new MagickGeometry((width - cropW) / 2, (height - cropH) / 2, (uint)cropW, (uint)cropH));
		image.ResetPage();
		image.Resize(new MagickGeometry(Width, Height) { IgnoreAspectRatio = true });
		image.Write(Path.Combine(destDir, uid + ".png"), MagickFormat.Png);
	}
}
