using System;
using System.Collections.Concurrent;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using Avalonia.Media.Imaging;
using editor.Assets;

namespace editor.Git;

public static class GravatarCache {
	private static readonly TimeSpan s_diskLifetime = TimeSpan.FromDays(7);
	private static readonly HttpClient s_http = CreateClient();
	private static readonly ConcurrentDictionary<string, Task<Bitmap?>> s_cache = new();

	/// <param name="email">Looked up on gravatar.com</param>
	/// <param name="githubLogin">Looked up on github.com when the gravatar is missing</param>
	public static Task<Bitmap?> GetAsync(string? email, string? githubLogin = null) {
		var login = githubLogin is not null && IsValidLogin(githubLogin) ? githubLogin : null;
		if (string.IsNullOrWhiteSpace(email) && login is null) return Task.FromResult<Bitmap?>(null);

		var key = (string.IsNullOrWhiteSpace(email) ? "" : Hash(email)) + "|" + login?.ToLowerInvariant();
		return s_cache.GetOrAdd(key, _ => Load(email, login));
	}

	public static string Hash(string email) {
		var bytes = MD5.HashData(Encoding.UTF8.GetBytes(email.Trim().ToLowerInvariant()));
		return Convert.ToHexString(bytes).ToLowerInvariant();
	}

	private static HttpClient CreateClient() {
		var client = new HttpClient { Timeout = TimeSpan.FromSeconds(10) };
		client.DefaultRequestHeaders.UserAgent.ParseAdd("ToastEditor/1.0");
		return client;
	}

	private static bool IsValidLogin(string login) {
		return login.Length is > 0 and <= 39 && login.All(c => char.IsAsciiLetterOrDigit(c) || c is '-' or '_');
	}

	private static async Task<Bitmap?> Load(string? email, string? githubLogin) {
		if (!string.IsNullOrWhiteSpace(email)) {
			var hash = Hash(email);
			// d=404 so an address without a gravatar answers 404
			var bitmap = await Fetch("gravatar_" + hash, $"https://www.gravatar.com/avatar/{hash}?s=64&d=404");
			if (bitmap is not null) return bitmap;
		}

		return githubLogin is null
			? null
			: await Fetch("github_" + githubLogin.ToLowerInvariant(), $"https://github.com/{githubLogin}.png?size=64");
	}

	private static async Task<Bitmap?> Fetch(string name, string url) {
		try {
			var path = ProjectContext.IsInitialized ? Path.Combine(ProjectContext.CachePath, "avatars", name + ".img") : null;

			if (path is not null && File.Exists(path) && DateTime.UtcNow - File.GetLastWriteTimeUtc(path) < s_diskLifetime)
				return new Bitmap(path);

			using var response = await s_http.GetAsync(url);
			if (!response.IsSuccessStatusCode) return null;

			var data = await response.Content.ReadAsByteArrayAsync();
			var bitmap = new Bitmap(new MemoryStream(data));
			if (path is not null) {
				Directory.CreateDirectory(Path.GetDirectoryName(path)!);
				await File.WriteAllBytesAsync(path, data);
			}

			return bitmap;
		} catch {
			// initials are shown instead
			return null;
		}
	}
}
