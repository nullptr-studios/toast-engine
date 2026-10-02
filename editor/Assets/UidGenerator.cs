using System;
using System.Diagnostics;
using System.Threading;

namespace editor.Assets;

public static class UidGenerator {
	private static long s_offset;

	public static string Generate() {
		var t = (ulong)Stopwatch.GetTimestamp();
		t = (t ^ (t >> 30)) * 0xbf58476d1ce4e5b9UL;
		t = (t ^ (t >> 27)) * 0x94d049bb133111ebUL;
		t ^= t >> 31;
		t += (ulong)(Interlocked.Increment(ref s_offset) - 1);

		return Encode(t);
	}

	public static string Encode(ulong value) {
		return Convert.ToBase64String(BitConverter.GetBytes(value))
			.Replace('+', '-')
			.Replace('/', '_')
			.TrimEnd('=');
	}

	private const string NativeCharset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

	public static string EncodeNative(ulong value) {
		Span<byte> b = stackalloc byte[8];
		for (var i = 0; i < 8; i++) b[i] = (byte)(value >> (56 - (i * 8)));

		Span<char> result = stackalloc char[11];
		result[0] = NativeCharset[b[0] >> 2];
		result[1] = NativeCharset[((b[0] & 0x03) << 4) | (b[1] >> 4)];
		result[2] = NativeCharset[((b[1] & 0x0f) << 2) | (b[2] >> 6)];
		result[3] = NativeCharset[b[2] & 0x3f];
		result[4] = NativeCharset[b[3] >> 2];
		result[5] = NativeCharset[((b[3] & 0x03) << 4) | (b[4] >> 4)];
		result[6] = NativeCharset[((b[4] & 0x0f) << 2) | (b[5] >> 6)];
		result[7] = NativeCharset[b[5] & 0x3f];
		result[8] = NativeCharset[b[6] >> 2];
		result[9] = NativeCharset[((b[6] & 0x03) << 4) | (b[7] >> 4)];
		result[10] = NativeCharset[(b[7] & 0x0f) << 2];
		return new string(result);
	}
}
