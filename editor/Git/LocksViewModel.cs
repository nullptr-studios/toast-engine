using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Components.Modals;
using editor.Workspace;

namespace editor.Git;

public partial class LockRow : GitFileRow {
	private static readonly string[] s_avatarColors = ["Red", "Green", "Blue", "Magenta", "Orange", "Cyan", "Beige"];

	private readonly LocksViewModel m_owner;
	[ObservableProperty] private Bitmap? m_avatar;

	public LockRow(GitLock heldLock, GitService git, LocksViewModel owner)
		: base(heldLock.Path, git.RepoRoot, git.GetAssetStatus(heldLock.Path)) {
		m_owner = owner;
		Lock = heldLock;
		Email = git.GetEmail(heldLock);
		Initials = MakeInitials(heldLock.Owner);
		InitialsBrush = GitStatusStyle.Brush(s_avatarColors[Math.Abs(heldLock.Owner.GetHashCode()) % s_avatarColors.Length]);
		UnlockCommand = new AsyncRelayCommand(() => m_owner.UnlockAsync(this));

		_ = LoadAvatar(Email, git.IsGitHub ? heldLock.Owner : null);
	}

	public GitLock Lock { get; }
	public string? Email { get; }
	public string Initials { get; }
	public IBrush InitialsBrush { get; }
	public bool IsMine => Lock.OwnedByMe;
	public bool HasAvatar => Avatar is not null;
	public string OwnerDetails => Email is null ? Lock.Owner : $"{Lock.Owner}\n{Email}";
	public IRelayCommand UnlockCommand { get; }

	partial void OnAvatarChanged(Bitmap? value) {
		OnPropertyChanged(nameof(HasAvatar));
	}

	private async Task LoadAvatar(string? email, string? githubLogin) {
		var bitmap = await GravatarCache.GetAsync(email, githubLogin);
		if (bitmap is null) return;
		Avalonia.Threading.Dispatcher.UIThread.Post(() => Avatar = bitmap);
	}

	private static string MakeInitials(string name) {
		var words = name.Split([' ', '.', '-', '_'], StringSplitOptions.RemoveEmptyEntries);
		return words.Length switch {
			0 => "?",
			1 => words[0][..Math.Min(2, words[0].Length)].ToUpperInvariant(),
			_ => (words[0][..1] + words[1][..1]).ToUpperInvariant()
		};
	}
}

public partial class LocksViewModel : Tool, IDisposable {
	private readonly GitService? m_git = GitService.Current;
	[ObservableProperty] private string m_summary = "No locks";

	public LocksViewModel() {
		if (m_git is null) return;
		m_git.LocksChanged += Rebuild;
		m_git.StatusChanged += Rebuild;
		Rebuild();
	}

	public ObservableCollection<LockRow> Rows { get; } = [];
	public bool HasRows => Rows.Count > 0;
	public string EmptyText => m_git is { IsAvailable: false }
		? "Git is not available for this project"
		: m_git is { HasLfs: false }
			? "git-lfs is not installed"
			: "Nothing is locked";

	public void Dispose() {
		if (m_git is not null) {
			m_git.LocksChanged -= Rebuild;
			m_git.StatusChanged -= Rebuild;
		}

		GC.SuppressFinalize(this);
	}

	[RelayCommand]
	private void LockArtwork() {
		MainWindowViewModel.Current?.LockArtworkCommand.Execute(null);
	}

	[RelayCommand]
	private async Task Refresh() {
		if (m_git is not null) await m_git.RefreshLocksAsync();
	}

	private bool CanUnlockAll() {
		return m_git is not null && Rows.Any(r => r.IsMine);
	}

	[RelayCommand(CanExecute = nameof(CanUnlockAll))]
	private async Task UnlockAll() {
		if (m_git is null) return;

		var mine = Rows.Where(r => r.IsMine).Select(r => r.Lock).ToList();
		if (mine.Count == 0) return;

		var confirmed = await App.Modals.ShowConfirm("Unlock all",
			$"Release your {mine.Count} lock{(mine.Count == 1 ? "" : "s")}? Anyone will be able to edit these files again.");
		if (!confirmed) return;

		var outcome = await m_git.UnlockManyAsync(mine);
		if (outcome.Failed > 0)
			await App.Modals.ShowError("Unlock all",
				$"Unlocked {outcome.Unlocked} of {mine.Count}. {outcome.Failed} could not be unlocked:\n{outcome.FirstError}");
	}

	private void Rebuild() {
		if (m_git is null) return;
		Rows.Clear();
		foreach (var held in m_git.Locks) Rows.Add(new LockRow(held, m_git, this));

		var mine = Rows.Count(r => r.IsMine);
		Summary = Rows.Count == 0 ? "No locks" : $"{Rows.Count} locked, {mine} by you";
		OnPropertyChanged(nameof(HasRows));
		OnPropertyChanged(nameof(EmptyText));
		UnlockAllCommand.NotifyCanExecuteChanged();
	}

	internal async Task UnlockAsync(LockRow row) {
		if (m_git is null) return;

		var result = await m_git.UnlockAsync(row.Key, false);
		if (!result.Ok) await App.Modals.ShowError("Cannot unlock " + row.Name, result.Message);
	}
}
