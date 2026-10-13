using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace editor.Git;

public partial class CommitViewModel : ObservableObject, IDisposable {
	private readonly GitService m_git;

	[ObservableProperty] [NotifyCanExecuteChangedFor(nameof(CommitCommand))] [NotifyCanExecuteChangedFor(nameof(CommitAndPushCommand))]
	private string m_message = "";

	[ObservableProperty] private string m_description = "";
	[ObservableProperty] private string m_error = "";
	[ObservableProperty] private string m_status = "";
	[ObservableProperty] private string m_branchText = "";

	[ObservableProperty] [NotifyCanExecuteChangedFor(nameof(CommitCommand))] [NotifyCanExecuteChangedFor(nameof(CommitAndPushCommand))]
	private bool m_isBusy;

	public CommitViewModel(GitService git) {
		m_git = git;
		m_git.StatusChanged += Rebuild;
		Rebuild();
	}

	public ObservableCollection<GitFileRow> Unstaged { get; } = [];
	public ObservableCollection<GitFileRow> Staged { get; } = [];

	public void Dispose() {
		m_git.StatusChanged -= Rebuild;
		GC.SuppressFinalize(this);
	}

	private void Rebuild() {
		var byKey = new Dictionary<string, List<GitChange>>(StringComparer.OrdinalIgnoreCase);
		foreach (var change in m_git.Changes) {
			var key = GitFileRow.KeyOf(change.Path);
			if (!byKey.TryGetValue(key, out var list)) byKey[key] = list = [];
			list.Add(change);
		}

		var unstaged = new List<GitFileRow>();
		var staged = new List<GitFileRow>();
		foreach (var (key, changes) in byKey) {
			var unstagedChanges = changes.Where(c => c.HasUnstaged).ToList();
			if (unstagedChanges.Count > 0) {
				var status = unstagedChanges.Aggregate(GitFileStatus.None, (acc, c) => acc | c.Unstaged);
				var row = new GitFileRow(key, m_git.RepoRoot, GitChange.Strongest(status));
				row.Members.AddRange(unstagedChanges);
				unstaged.Add(row);
			}

			var stagedChanges = changes.Where(c => c.HasStaged).ToList();
			if (stagedChanges.Count > 0) {
				var status = stagedChanges.Aggregate(GitFileStatus.None, (acc, c) => acc | c.Staged);
				var row = new GitFileRow(key, m_git.RepoRoot, GitChange.Strongest(status));
				row.Members.AddRange(stagedChanges);
				staged.Add(row);
			}
		}

		Fill(Unstaged, unstaged);
		Fill(Staged, staged);

		var branch = m_git.Branch;
		BranchText = branch.HasUpstream ? $"{branch.Name}   ↑{branch.Ahead}  ↓{branch.Behind}" : branch.Name;
		CommitCommand.NotifyCanExecuteChanged();
		CommitAndPushCommand.NotifyCanExecuteChanged();
	}

	private static void Fill(ObservableCollection<GitFileRow> target, List<GitFileRow> rows) {
		target.Clear();
		foreach (var row in rows.OrderBy(r => r.RelPath, StringComparer.OrdinalIgnoreCase)) target.Add(row);
	}

	public Task StageAsync(IEnumerable<GitFileRow> rows) {
		var paths = rows.SelectMany(r => r.Members).Where(m => m.HasUnstaged).Select(m => m.Path).ToList();
		return Run(() => m_git.StageAsync(paths));
	}

	public Task UnstageAsync(IEnumerable<GitFileRow> rows) {
		// A staged rename is two paths
		var paths = rows.SelectMany(r => r.Members).Where(m => m.HasStaged)
			.SelectMany(m => m.OldPath is null ? [m.Path] : new[] { m.Path, m.OldPath }).ToList();
		return Run(() => m_git.UnstageAsync(paths));
	}

	[RelayCommand]
	private Task StageAll() {
		return StageAsync(Unstaged.ToList());
	}

	[RelayCommand]
	private Task UnstageAll() {
		return UnstageAsync(Staged.ToList());
	}

	public async Task DiscardAsync(IReadOnlyCollection<GitFileRow> rows) {
		if (rows.Count == 0 || IsBusy) return;

		var created = rows.Count(r => r.Members.Any(m => (m.Unstaged & (GitFileStatus.Untracked | GitFileStatus.Moved)) != 0));
		var message = $"Discard the changes to {rows.Count} file{(rows.Count == 1 ? "" : "s")}? This can't be undone.";
		if (created > 0)
			message += $"\n\n{created} new file{(created == 1 ? " is" : "s are")} not in git yet and will be deleted.";
		if (!await App.Modals.ShowConfirm("Discard changes", message)) return;

		var members = rows.SelectMany(r => r.Members).ToList();
		await Run(() => m_git.DiscardAsync(members));
	}

	[RelayCommand]
	private Task DiscardAll() {
		return DiscardAsync(Unstaged.ToList());
	}

	private bool CanCommit() {
		return !IsBusy && Staged.Count > 0 && Message.Trim().Length > 0;
	}

	[RelayCommand(CanExecute = nameof(CanCommit))]
	private async Task Commit() {
		await Run(async () => {
			var result = await m_git.CommitAsync(Message, Description);
			if (result.Ok) {
				Message = "";
				Description = "";
			}

			return result;
		});
	}

	[RelayCommand(CanExecute = nameof(CanCommit))]
	private async Task CommitAndPush() {
		await Run(async () => {
			var commit = await m_git.CommitAsync(Message, Description);
			if (!commit.Ok) return commit;

			Message = "";
			Description = "";
			Status = "Pushing...";
			var push = await m_git.PushAsync();
			return push.Ok ? push : new GitResult(push.ExitCode, "", "Committed, but the push failed:\n" + push.Message);
		});
	}

	private async Task Run(Func<Task<GitResult>> action) {
		if (IsBusy) return;
		IsBusy = true;
		Error = "";
		try {
			var result = await action();
			if (!result.Ok) Error = result.Message;
		} finally {
			Status = "";
			IsBusy = false;
		}
	}
}
