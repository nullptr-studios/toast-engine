using System;
using CommunityToolkit.Mvvm.ComponentModel;

namespace editor.Git;

public partial class GitViewModel : ObservableObject, IDisposable {
	[ObservableProperty] private int m_ahead;
	[ObservableProperty] private int m_behind;
	[ObservableProperty] private string m_branchName = "";
	[ObservableProperty] private bool m_isAvailable;
	[ObservableProperty] private string m_toolTip = "";

	public GitViewModel(GitService service) {
		Service = service;
		Service.StatusChanged += Refresh;
		Refresh();
	}

	public GitService Service { get; }

	public bool HasAhead => Ahead > 0;
	public bool HasBehind => Behind > 0;

	public void Dispose() {
		Service.StatusChanged -= Refresh;
		GC.SuppressFinalize(this);
	}

	partial void OnAheadChanged(int value) {
		OnPropertyChanged(nameof(HasAhead));
	}

	partial void OnBehindChanged(int value) {
		OnPropertyChanged(nameof(HasBehind));
	}

	private void Refresh() {
		var branch = Service.Branch;
		IsAvailable = Service.IsAvailable;
		BranchName = branch.Name;
		Ahead = branch.Ahead;
		Behind = branch.Behind;

		var fetched = Service.LastFetch is { } at ? $"Last fetched {at:HH:mm}" : "Not fetched yet";
		ToolTip = branch.HasUpstream
			? $"{branch.Name}: {branch.Ahead} to push, {branch.Behind} to pull\n{fetched}\nClick to open the commit window"
			: $"{branch.Name}: no upstream branch\nClick to open the commit window";
	}
}
