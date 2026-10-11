using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Threading;
using editor.Engine;

namespace editor.Git;

public sealed class GitService : IDisposable {
	private static readonly TimeSpan s_fetchInterval = TimeSpan.FromMinutes(5);
	private static readonly TimeSpan s_locksInterval = TimeSpan.FromMinutes(1);

	private sealed record StatusSnapshot(
		GitBranchInfo Branch,
		IReadOnlyDictionary<string, GitChange> Changes,
		IReadOnlyDictionary<string, GitFileStatus> Folders) {
		public static readonly StatusSnapshot Empty = new(
			GitBranchInfo.None,
			new Dictionary<string, GitChange>(),
			new Dictionary<string, GitFileStatus>());
	}

	private sealed record LockSnapshot(IReadOnlyDictionary<string, GitLock> ByPath, IReadOnlyList<GitLock> All) {
		public static readonly LockSnapshot Empty = new(new Dictionary<string, GitLock>(), []);
	}

	private readonly string m_projectPath;
	private readonly CancellationTokenSource m_cts = new();
	private readonly object m_watchLock = new();
	private readonly List<FileSystemWatcher> m_watchers = [];

	private DispatcherTimer? m_fetchTimer;
	private DispatcherTimer? m_locksTimer;
	private Timer? m_debounce;

	private volatile StatusSnapshot m_status = StatusSnapshot.Empty;
	private volatile LockSnapshot m_locks = LockSnapshot.Empty;
	private volatile GitAttributes? m_attributes;
	private volatile Dictionary<string, string> m_authors = new(StringComparer.OrdinalIgnoreCase);

	private int m_statusRunning;
	private int m_statusPending;
	private int m_locksRunning;
	private int m_fetchRunning;
	private bool m_disposed;
	private DateTime m_authorsLoadedAt = DateTime.MinValue;
	private bool m_locksFailing;
	private bool m_fetchFailing;
	private string m_userName = "";
	private string m_userEmail = "";

	private GitService(string projectPath) {
		m_projectPath = projectPath;
	}

	public static GitService? Current { get; private set; }
	public bool IsAvailable { get; private set; }
	public bool HasLfs { get; private set; }
	public string RepoRoot { get; private set; } = "";
	public DateTime? LastFetch { get; private set; }
	public GitBranchInfo Branch => m_status.Branch;
	public IReadOnlyCollection<GitChange> Changes => m_status.Changes.Values.ToList();
	public IReadOnlyList<GitLock> Locks => m_locks.All;
	public string UserName => m_userName;
	public event Action? StatusChanged;
	public event Action? LocksChanged;

	public static GitService Start(string projectPath) {
		Current?.Dispose();
		var service = new GitService(projectPath);
		Current = service;
		_ = service.InitializeAsync();
		return service;
	}

	public void Dispose() {
		if (m_disposed) return;
		m_disposed = true;
		if (ReferenceEquals(Current, this)) Current = null;
		m_cts.Cancel();
		m_fetchTimer?.Stop();
		m_locksTimer?.Stop();
		m_debounce?.Dispose();
		lock (m_watchLock) {
			foreach (var watcher in m_watchers) watcher.Dispose();
			m_watchers.Clear();
		}

		m_cts.Dispose();
	}

	public GitChange? GetChange(string absolutePath) {
		return m_status.Changes.GetValueOrDefault(Normalize(absolutePath));
	}

	public GitFileStatus GetAssetStatus(string assetPath) {
		var asset = GetChange(assetPath)?.Display ?? GitFileStatus.None;
		var meta = GetChange(assetPath + ".meta")?.Display ?? GitFileStatus.None;
		return GitChange.Strongest(Visible(asset) | Visible(meta));
	}

	public GitFileStatus GetFolderStatus(string folderPath) {
		return m_status.Folders.GetValueOrDefault(Normalize(folderPath));
	}

	public GitLock? GetLock(string absolutePath) {
		return m_locks.ByPath.GetValueOrDefault(Normalize(absolutePath));
	}

	public bool IsLockable(string absolutePath) {
		return HasLfs && (m_attributes?.IsLockable(absolutePath) == true || GetLock(absolutePath) is not null);
	}

	public bool IsGitHub { get; private set; }

	public string? GetEmail(GitLock held) {
		if (held.OwnedByMe && m_userEmail.Length > 0) return m_userEmail;
		return GitParsers.FindEmail(m_authors, held.Owner);
	}

	private static GitFileStatus Visible(GitFileStatus status) {
		// Deleted files have no card
		return status == GitFileStatus.Deleted ? GitFileStatus.None : status;
	}

	private static string Normalize(string path) {
		return Path.GetFullPath(path);
	}

	private async Task InitializeAsync() {
		try {
			var top = await RunAsync(["rev-parse", "--show-toplevel"], serialize: false);
			if (!top.Ok || string.IsNullOrWhiteSpace(top.Output)) {
				Log.Info("Git: project is not in a git repository, git features are disabled");
				return;
			}

			RepoRoot = Path.GetFullPath(top.Output.Trim());
			var lfs = await RunAsync(["lfs", "version"], serialize: false);
			HasLfs = lfs.Ok;
			if (!HasLfs) Log.Warn("Git: git-lfs is not installed, lock features are disabled");

			m_userName = (await RunAsync(["config", "user.name"], serialize: false)).Output.Trim();
			m_userEmail = (await RunAsync(["config", "user.email"], serialize: false)).Output.Trim();
			var origin = (await RunAsync(["remote", "get-url", "origin"], serialize: false)).Output;
			IsGitHub = origin.Contains("github.com", StringComparison.OrdinalIgnoreCase);
			m_attributes = GitAttributes.Load(RepoRoot, m_projectPath);
			IsAvailable = true;

			Dispatcher.UIThread.Post(StartBackgroundWork);
			await RefreshStatusAsync();
			if (HasLfs) await RefreshLocksAsync();
			// The first refresh may find nothing to report
			Dispatcher.UIThread.Post(() => {
				StatusChanged?.Invoke();
				LocksChanged?.Invoke();
			});
			_ = FetchAsync();
		} catch (Exception e) {
			Log.Warn($"Git: failed to initialize ({e.Message})");
		}
	}

	private void StartBackgroundWork() {
		if (m_disposed) return;

		m_fetchTimer = new DispatcherTimer { Interval = s_fetchInterval };
		m_fetchTimer.Tick += (_, _) => _ = FetchAsync();
		m_fetchTimer.Start();

		if (HasLfs) {
			m_locksTimer = new DispatcherTimer { Interval = s_locksInterval };
			m_locksTimer.Tick += (_, _) => _ = RefreshLocksAsync();
			m_locksTimer.Start();
		}

		StartWatching();
	}

	private void StartWatching() {
		m_debounce = new Timer(_ => {
			m_attributes = GitAttributes.Load(RepoRoot, m_projectPath);
			_ = RefreshStatusAsync();
		});

		try {
			var project = new FileSystemWatcher(m_projectPath) {
				IncludeSubdirectories = true,
				NotifyFilter = NotifyFilters.FileName | NotifyFilters.DirectoryName | NotifyFilters.LastWrite
			};
			project.Changed += OnWatchEvent;
			project.Created += OnWatchEvent;
			project.Deleted += OnWatchEvent;
			project.Renamed += OnWatchEvent;
			project.EnableRaisingEvents = true;

			// Staging, committing or switching branches from outside the editor only touches the .git folder
			var gitDir = Path.Combine(RepoRoot, ".git");
			var index = new FileSystemWatcher(gitDir) { NotifyFilter = NotifyFilters.FileName | NotifyFilters.LastWrite };
			index.Changed += OnGitDirEvent;
			index.Created += OnGitDirEvent;
			index.Renamed += OnGitDirEvent;
			index.EnableRaisingEvents = true;

			lock (m_watchLock) {
				m_watchers.Add(project);
				m_watchers.Add(index);
			}
		} catch (Exception e) {
			Log.Warn($"Git: could not watch the project folder ({e.Message})");
		}
	}

	private void OnWatchEvent(object sender, FileSystemEventArgs e) {
		var path = e.FullPath;
		var sep = Path.DirectorySeparatorChar;
		if (path.Contains($"{sep}.git{sep}") || path.Contains($"{sep}.toast{sep}")) return;
		Debounce();
	}

	private void OnGitDirEvent(object sender, FileSystemEventArgs e) {
		var name = Path.GetFileName(e.FullPath);
		// Ignore our own fetch/lock noise
		if (name is "index" or "HEAD" or "MERGE_HEAD" or "ORIG_HEAD") Debounce();
	}

	private void Debounce() {
		if (m_disposed) return;
		try {
			m_debounce?.Change(300, Timeout.Infinite);
		} catch (ObjectDisposedException) {
			// shutting down
		}
	}

	public async Task RefreshStatusAsync() {
		if (!IsAvailable || m_disposed) return;

		// Coalesce overlapping refreshes into one extra pass
		if (Interlocked.Exchange(ref m_statusRunning, 1) == 1) {
			Interlocked.Exchange(ref m_statusPending, 1);
			return;
		}

		try {
			do {
				Interlocked.Exchange(ref m_statusPending, 0);
				try {
					await RefreshStatusOnce();
				} catch (Exception e) {
					// Callers fire and forget
					Log.Warn($"Git: status refresh failed ({e.Message})");
				}
			} while (Interlocked.CompareExchange(ref m_statusPending, 0, 1) == 1 && !m_disposed);
		} finally {
			Interlocked.Exchange(ref m_statusRunning, 0);
		}
	}

	private async Task RefreshStatusOnce() {
		var result = await RunAsync(["status", "--porcelain=v2", "-z", "--branch", "--untracked-files=all"]);
		if (!result.Ok) {
			Log.Warn($"Git: status failed ({result.Message})");
			return;
		}

		var snapshot = GitParsers.ParseStatus(result.Output, RepoRoot);
		var changes = await PairMoves(snapshot.Changes);

		var map = new Dictionary<string, GitChange>(StringComparer.OrdinalIgnoreCase);
		foreach (var change in changes) map[change.Path] = change;

		var next = new StatusSnapshot(snapshot.Branch, map, BuildFolderStatus(map.Values));
		var previous = m_status;
		m_status = next;
		if (!Equivalent(previous, next)) Dispatcher.UIThread.Post(() => StatusChanged?.Invoke());
	}

	private static bool Equivalent(StatusSnapshot a, StatusSnapshot b) {
		if (a.Branch != b.Branch || a.Changes.Count != b.Changes.Count) return false;
		foreach (var (path, change) in a.Changes)
			if (!b.Changes.TryGetValue(path, out var other) || other != change)
				return false;
		return true;
	}

	private async Task<IReadOnlyList<GitChange>> PairMoves(IReadOnlyList<GitChange> changes) {
		var deletedMetas = changes
			.Where(c => c.Path.EndsWith(".meta", StringComparison.OrdinalIgnoreCase) &&
			            ((c.Staged | c.Unstaged) & GitFileStatus.Deleted) != 0)
			.Select(c => GitParsers.Rel(RepoRoot, c.Path))
			.ToList();
		var hasNewMetas = changes.Any(c => c.Path.EndsWith(".meta", StringComparison.OrdinalIgnoreCase) &&
		                                   (c.Unstaged == GitFileStatus.Untracked ||
		                                    (c.Staged & GitFileStatus.Added) != 0));
		if (deletedMetas.Count == 0 || !hasNewMetas) return changes;

		var oldUids = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
		foreach (var chunk in deletedMetas.Chunk(100)) {
			var args = new List<string> { "--literal-pathspecs", "grep", "--no-color", "-n", "-e", "^uid", "HEAD", "--" };
			args.AddRange(chunk);
			var grep = await RunAsync(args);
			if (!grep.Ok) continue;
			foreach (var kv in GitParsers.ParseGrepUids(grep.Output)) oldUids[kv.Key] = kv.Value;
		}

		if (oldUids.Count == 0) return changes;
		return GitParsers.PairMoves(changes, oldUids, ReadUid);
	}

	private static string? ReadUid(string metaPath) {
		try {
			foreach (var line in File.ReadLines(metaPath)) {
				var trimmed = line.Trim();
				if (!trimmed.StartsWith("uid", StringComparison.Ordinal)) continue;
				var eq = trimmed.IndexOf('=');
				if (eq < 0) continue;
				return trimmed[(eq + 1)..].Trim().Trim('"');
			}
		} catch {
			// unreadable
		}

		return null;
	}

	private Dictionary<string, GitFileStatus> BuildFolderStatus(IEnumerable<GitChange> changes) {
		var folders = new Dictionary<string, GitFileStatus>(StringComparer.OrdinalIgnoreCase);
		var root = RepoRoot.TrimEnd(Path.DirectorySeparatorChar);
		foreach (var change in changes) {
			var status = Visible(change.Display);
			if (status == GitFileStatus.None) continue;

			var dir = Path.GetDirectoryName(change.Path);
			while (!string.IsNullOrEmpty(dir) && dir.Length >= root.Length) {
				folders[dir] = GitChange.Strongest(folders.GetValueOrDefault(dir) | status);
				if (dir.Length == root.Length) break;
				dir = Path.GetDirectoryName(dir);
			}
		}

		return folders;
	}

	public async Task RefreshLocksAsync() {
		if (!IsAvailable || !HasLfs || m_disposed) return;
		if (Interlocked.Exchange(ref m_locksRunning, 1) == 1) return;

		try {
			var result = await RunAsync(["lfs", "locks", "--verify", "--json"], serialize: false,
				timeout: TimeSpan.FromSeconds(30));
			if (!result.Ok)
				result = await RunAsync(["lfs", "locks", "--json"], serialize: false, timeout: TimeSpan.FromSeconds(30));
			if (!result.Ok) {
				if (!m_locksFailing) Log.Warn($"Git: could not fetch LFS locks ({result.Message})");
				m_locksFailing = true;
				return;
			}

			m_locksFailing = false;
			var locks = GitParsers.ParseLocks(result.Output, RepoRoot, m_userName);
			await LoadAuthors(locks);

			var map = new Dictionary<string, GitLock>(StringComparer.OrdinalIgnoreCase);
			foreach (var l in locks) map[l.Path] = l;
			var previous = m_locks;
			m_locks = new LockSnapshot(map, locks.OrderByDescending(l => l.OwnedByMe).ThenBy(l => l.Path).ToList());

			var changed = previous.All.Count != locks.Count ||
			              locks.Any(l => !previous.ByPath.TryGetValue(l.Path, out var old) || old != l);
			if (changed) Dispatcher.UIThread.Post(() => LocksChanged?.Invoke());
		} catch (Exception e) {
			Log.Warn($"Git: lock refresh failed ({e.Message})");
		} finally {
			Interlocked.Exchange(ref m_locksRunning, 0);
		}
	}

	private async Task LoadAuthors(IReadOnlyCollection<GitLock> locks) {
		// Authors only change when people commit
		if (locks.Count == 0 || DateTime.UtcNow - m_authorsLoadedAt < TimeSpan.FromMinutes(10)) return;
		m_authorsLoadedAt = DateTime.UtcNow;

		var log = await RunAsync(["log", "--all", "--max-count=5000", "--format=%an%x09%ae"], serialize: false,
			timeout: TimeSpan.FromSeconds(20));
		if (log.Ok) m_authors = GitParsers.ParseAuthors(log.Output);
	}

	public async Task<GitResult> FetchAsync() {
		if (!IsAvailable || m_disposed) return new GitResult(-1, "", "git is not available");
		if (Interlocked.Exchange(ref m_fetchRunning, 1) == 1) return new GitResult(0, "", "");

		try {
			var result = await RunAsync(["fetch", "--prune"], serialize: false, timeout: TimeSpan.FromMinutes(5));
			if (result.Ok) {
				m_fetchFailing = false;
				LastFetch = DateTime.Now;
				await RefreshStatusAsync();
			} else {
				if (!m_fetchFailing) Log.Warn($"Git: fetch failed ({result.Message})");
				m_fetchFailing = true;
			}

			return result;
		} finally {
			Interlocked.Exchange(ref m_fetchRunning, 0);
		}
	}

	public async Task<GitResult> PullAsync() {
		var result = await RunAsync(["pull", "--ff-only"], timeout: TimeSpan.FromMinutes(30));
		if (result.Ok) LastFetch = DateTime.Now;
		await RefreshStatusAsync();
		if (HasLfs) _ = RefreshLocksAsync();
		return result;
	}

	public async Task<GitResult> PushAsync() {
		var result = await RunAsync(["push"], timeout: TimeSpan.FromMinutes(30));
		if (!result.Ok && !Branch.HasUpstream && Branch.Name.Length > 0 && !Branch.Name.StartsWith("HEAD"))
			result = await RunAsync(["push", "--set-upstream", "origin", Branch.Name], timeout: TimeSpan.FromMinutes(30));
		await RefreshStatusAsync();
		return result;
	}

	public async Task<GitResult> StageAsync(IEnumerable<string> absolutePaths) {
		var result = await RunPathspecAsync(["add"], absolutePaths);
		await RefreshStatusAsync();
		return result;
	}

	public async Task<GitResult> UnstageAsync(IEnumerable<string> absolutePaths) {
		var result = await RunPathspecAsync(["reset", "-q"], absolutePaths);
		await RefreshStatusAsync();
		return result;
	}

	public async Task<GitResult> DiscardAsync(IEnumerable<GitChange> changes) {
		var restore = new List<string>();
		var created = new List<string>();
		foreach (var change in changes) {
			if (!change.HasUnstaged) continue;
			// A move paired by UID is a new file as far as git is concerned
			if ((change.Unstaged & (GitFileStatus.Untracked | GitFileStatus.Moved)) != 0) created.Add(change.Path);
			else restore.Add(change.Path);
		}

		var result = new GitResult(0, "", "");
		foreach (var path in created) {
			try {
				File.SetAttributes(path, FileAttributes.Normal);
				File.Delete(path);
			} catch (Exception e) {
				result = new GitResult(-1, "", $"Could not delete {Path.GetFileName(path)}: {e.Message}");
			}
		}

		if (restore.Count > 0) {
			var restored = await RunPathspecAsync(["restore"], restore);
			if (!restored.Ok) result = restored;
		}

		await RefreshStatusAsync();
		return result;
	}

	public async Task<GitResult> CommitAsync(string message, string description) {
		var text = string.IsNullOrWhiteSpace(description)
			? message.Trim()
			: message.Trim() + "\n\n" + description.Trim();
		var result = await RunAsync(["commit", "-F", "-"], stdin: text);
		await RefreshStatusAsync();
		return result;
	}

	public async Task<GitResult> LockAsync(string absolutePath) {
		var result = await RunAsync(["lfs", "lock", "--", GitParsers.Rel(RepoRoot, absolutePath)], serialize: false,
			timeout: TimeSpan.FromSeconds(60));
		await RefreshLocksAsync();
		return result;
	}

	public List<string> FindLockableFiles(string folder) {
		var result = new List<string>();
		if (!HasLfs || !Directory.Exists(folder)) return result;

		var options = new EnumerationOptions { RecurseSubdirectories = true, IgnoreInaccessible = true };
		foreach (var file in Directory.EnumerateFiles(folder, "*", options)) {
			if (file.EndsWith(".meta", StringComparison.OrdinalIgnoreCase)) continue;
			if (IsLockable(file) && GetLock(file) is null) result.Add(file);
		}

		return result;
	}

	public async Task<LockManyResult> LockManyAsync(IReadOnlyList<string> absolutePaths, Action<int, int>? progress = null) {
		var locked = 0;
		var failed = 0;
		var done = 0;
		string? firstError = null;

		using var gate = new SemaphoreSlim(4);
		await Task.WhenAll(absolutePaths.Select(async path => {
			await gate.WaitAsync();
			try {
				var result = await RunAsync(["lfs", "lock", "--", GitParsers.Rel(RepoRoot, path)], serialize: false,
					timeout: TimeSpan.FromSeconds(60));
				if (result.Ok) {
					Interlocked.Increment(ref locked);
				} else {
					Interlocked.Increment(ref failed);
					Interlocked.CompareExchange(ref firstError, result.Message, null);
				}
			} finally {
				gate.Release();
				progress?.Invoke(Interlocked.Increment(ref done), absolutePaths.Count);
			}
		}));

		await RefreshLocksAsync();
		return new LockManyResult(locked, failed, firstError);
	}

	public async Task<LockManyResult> UnlockManyAsync(IReadOnlyList<GitLock> locks) {
		var unlocked = 0;
		var failed = 0;
		string? firstError = null;

		using var gate = new SemaphoreSlim(4);
		await Task.WhenAll(locks.Select(async held => {
			await gate.WaitAsync();
			try {
				// By id
				var args = held.Id.Length > 0
					? new List<string> { "lfs", "unlock", "--id=" + held.Id }
					: ["lfs", "unlock", "--", GitParsers.Rel(RepoRoot, held.Path)];
				var result = await RunUnlock(args, held.Id);
				if (result.Ok) {
					Interlocked.Increment(ref unlocked);
				} else {
					Interlocked.Increment(ref failed);
					Interlocked.CompareExchange(ref firstError, result.Message, null);
				}
			} finally {
				gate.Release();
			}
		}));

		await RefreshLocksAsync();
		return new LockManyResult(unlocked, failed, firstError);
	}

	public async Task<GitResult> UnlockAsync(string absolutePath, bool force) {
		var existing = GetLock(absolutePath);
		var args = new List<string> { "lfs", "unlock" };
		if (force) args.Add("--force");
		if (existing is { Id.Length: > 0 }) args.Add("--id=" + existing.Id);
		else args.AddRange(["--", GitParsers.Rel(RepoRoot, absolutePath)]);

		var result = await RunUnlock(args, existing?.Id);
		await RefreshLocksAsync();
		return result;
	}

	// git-lfs releases the lock on the server first and only then looks at the file
	private async Task<GitResult> RunUnlock(List<string> args, string? id) {
		var result = await RunAsync(args, serialize: false, timeout: TimeSpan.FromSeconds(60));
		if (result.Ok || !IsMissingFileError(result.Message)) return result;

		if (!string.IsNullOrEmpty(id)) {
			var check = await RunAsync(["lfs", "locks", "--id=" + id, "--json"], serialize: false,
				timeout: TimeSpan.FromSeconds(30));
			if (check.Ok && check.Output.Trim() is "[]" or "") return new GitResult(0, "Unlocked", "");
		}

		if (args.Contains("--force")) return result;
		var forced = new List<string>(args) { "--force" };
		return await RunAsync(forced, serialize: false, timeout: TimeSpan.FromSeconds(60));
	}

	private static bool IsMissingFileError(string message) {
		return message.Contains("cannot find the file", StringComparison.OrdinalIgnoreCase) ||
		       message.Contains("no such file", StringComparison.OrdinalIgnoreCase) ||
		       message.Contains("GetFileAttributesEx", StringComparison.OrdinalIgnoreCase);
	}

	private Task<GitResult> RunAsync(IEnumerable<string> args, string? stdin = null, bool serialize = true,
		TimeSpan? timeout = null) {
		if (m_disposed) return Task.FromResult(new GitResult(-1, "", "cancelled"));
		var cwd = RepoRoot.Length > 0 ? RepoRoot : m_projectPath;
		try {
			return GitRunner.RunAsync(cwd, args, stdin, serialize, timeout, m_cts.Token);
		} catch (ObjectDisposedException) {
			return Task.FromResult(new GitResult(-1, "", "cancelled"));
		}
	}

	private Task<GitResult> RunPathspecAsync(IEnumerable<string> command, IEnumerable<string> absolutePaths) {
		var paths = absolutePaths.Select(p => GitParsers.Rel(RepoRoot, p)).Distinct().ToList();
		if (paths.Count == 0) return Task.FromResult(new GitResult(0, "", ""));

		var args = new List<string> { "--literal-pathspecs" };
		args.AddRange(command);
		args.AddRange(["--pathspec-from-file=-", "--pathspec-file-nul"]);
		var stdin = new StringBuilder();
		foreach (var path in paths) stdin.Append(path).Append('\0');
		return RunAsync(args, stdin.ToString());
	}
}
