using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using ImageMagick;

namespace editor.Assets.Importers;

internal static class KtxWriter {
	private static bool IsWindows => RuntimeInformation.IsOSPlatform(OSPlatform.Windows);

	private static string NativeDir => Path.Combine(AppContext.BaseDirectory, "native");
	private static string ToktxPath => Path.Combine(NativeDir, IsWindows ? "toktx.exe" : "toktx");

	public static async Task ConvertTexture(
		string srcPath, string destPath, TextureImporter.Settings s, Action<string>? log = null) {
		if (string.IsNullOrWhiteSpace(srcPath) || !File.Exists(srcPath))
			throw new FileNotFoundException($"Source image not found: '{srcPath}'");
		if (string.IsNullOrWhiteSpace(destPath))
			throw new ArgumentException("Destination path is empty.", nameof(destPath));

		var destDir = Path.GetDirectoryName(Path.GetFullPath(destPath));
		if (!string.IsNullOrEmpty(destDir)) Directory.CreateDirectory(destDir);

		// toktx only accepts PNG as input (it doesnt understand PSD, TGA with alpha, etc)
		// so we normalize everything to PNG first via ImageMagick
		var tempPng = Path.Combine(Path.GetTempPath(), $"toast_import_{Guid.NewGuid():N}.png");
		try {
			PrepareImage(srcPath, tempPng, s, log);
			var args = BuildArgs(s, destPath, tempPng);
			log?.Invoke($"> toktx {args}");
			await RunToktx(args, log);
		} finally {
			try {
				if (File.Exists(tempPng)) File.Delete(tempPng);
			} catch {
				/* best effort */
			}
		}
	}

	private static void PrepareImage(string srcPath, string tempPng, TextureImporter.Settings s, Action<string>? log) {
		using var image = new MagickImage(srcPath);
		image.ColorSpace = ColorSpace.sRGB;

		if (image.Width > (uint)s.MaxResolution || image.Height > (uint)s.MaxResolution) {
			log?.Invoke($"Resizing to {s.MaxResolution}px max...");
			image.Resize(new MagickGeometry((uint)s.MaxResolution, (uint)s.MaxResolution) {
				IgnoreAspectRatio = false
			});
		}

		image.Write(tempPng, MagickFormat.Png);
	}

	private static string BuildArgs(TextureImporter.Settings s, string destPath, string inputPath) {
		var sb = new StringBuilder("--t2 ");

		if (s.GenerateMipmaps)
			sb.Append("--genmipmap ");

		// ReSharper disable once InconsistentNaming
		var isEtc1s = false;
		// BC7 and ASTC arent direct KTX2 encodings -> store as UASTC basis
		// and let the runtime transcode to whatever the GPU actually supports
		switch (s.Compression) {
			case TextureCompression.BC7:
			case TextureCompression.ASTC:
				sb.Append("--encode uastc ");
				break;
			case TextureCompression.BC1:
			case TextureCompression.BC3:
			case TextureCompression.BC4:
			case TextureCompression.BC5:
				sb.Append("--encode etc1s ");
				isEtc1s = true;
				break;
			case TextureCompression.None:
			default:
				break;
		}

		// etc1s has builtin compression, zstd on top does nothing useful
		if (s.SuperCompression == SuperCompression.Zstd && !isEtc1s)
			sb.Append("--zcmp 5 ");

		sb.Append($"\"{destPath}\" \"{inputPath}\"");
		return sb.ToString();
	}

	private static async Task RunToktx(string args, Action<string>? log) {
		var exe = ToktxPath;
		if (!File.Exists(exe))
			throw new FileNotFoundException(
				$"toktx not found at '{exe}'. Place the KTX-Software 4.x binaries in editor/native/win-x64/ (or linux-x64/) — they are copied to the output's native/ folder at build time.");

		var ktxLib = Path.Combine(NativeDir, IsWindows ? "ktx.dll" : "libktx.so");
		if (!File.Exists(ktxLib))
			throw new FileNotFoundException(
				$"'{Path.GetFileName(ktxLib)}' not found next to toktx at '{NativeDir}'. " +
				"It must sit beside toktx or texture conversion will fail to launch.");

		var psi = new ProcessStartInfo {
			FileName = exe,
			Arguments = args,
			UseShellExecute = false,
			RedirectStandardOutput = true,
			RedirectStandardError = true,
			CreateNoWindow = true
		};

		using var proc = Process.Start(psi)
		                 ?? throw new InvalidOperationException("Failed to start toktx");

		var lastError = "";
		proc.OutputDataReceived += (_, e) => {
			if (e.Data is not null) log?.Invoke(e.Data);
		};
		proc.ErrorDataReceived += (_, e) => {
			if (e.Data is null) return;
			log?.Invoke(e.Data);
			if (e.Data.Trim().Length > 0) lastError = e.Data.Trim();
		};
		proc.BeginOutputReadLine();
		proc.BeginErrorReadLine();

		await proc.WaitForExitAsync();

		if (proc.ExitCode != 0)
			throw new InvalidOperationException(
				$"toktx exited with code {proc.ExitCode}" +
				(lastError.Length > 0 ? $": {lastError}" : "."));
	}
}
