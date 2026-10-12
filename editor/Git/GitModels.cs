using System;

namespace editor.Git;

[Flags]
public enum GitFileStatus {
	None = 0,
	Added = 1,
	Modified = 2,
	Moved = 4,
	Untracked = 8,
	Deleted = 16
}

public sealed record GitChange(string Path, GitFileStatus Staged, GitFileStatus Unstaged, string? OldPath = null) {
	public bool HasStaged => Staged != GitFileStatus.None;
	public bool HasUnstaged => Unstaged != GitFileStatus.None;

	public GitFileStatus Display => Strongest(Staged | Unstaged);

	public static GitFileStatus Strongest(GitFileStatus status) {
		if ((status & GitFileStatus.Moved) != 0) return GitFileStatus.Moved;
		if ((status & GitFileStatus.Added) != 0) return GitFileStatus.Added;
		if ((status & GitFileStatus.Modified) != 0) return GitFileStatus.Modified;
		if ((status & GitFileStatus.Untracked) != 0) return GitFileStatus.Untracked;
		if ((status & GitFileStatus.Deleted) != 0) return GitFileStatus.Deleted;
		return GitFileStatus.None;
	}
}

public sealed record GitLock(string Id, string Path, string Owner, bool OwnedByMe, DateTime LockedAt);

public sealed record GitBranchInfo(string Name, int Ahead, int Behind, bool HasUpstream) {
	public static readonly GitBranchInfo None = new("", 0, 0, false);
}

public sealed record LockManyResult(int Locked, int Failed, string? FirstError) {
	public int Unlocked => Locked;
}

public readonly record struct GitResult(int ExitCode, string Output, string Error) {
	public bool Ok => ExitCode == 0;

	public string Message {
		get {
			var text = string.IsNullOrWhiteSpace(Error) ? Output : Error;
			return text.Trim();
		}
	}
}
