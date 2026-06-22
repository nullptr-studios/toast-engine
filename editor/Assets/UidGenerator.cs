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

		return Convert.ToBase64String(BitConverter.GetBytes(t))
			.Replace('+', '-')
			.Replace('/', '_')
			.TrimEnd('=');
	}
}
