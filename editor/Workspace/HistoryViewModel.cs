using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using Lucide.Avalonia;
using Proto.Events;

namespace editor.Workspace;

public partial class HistoryRowViewModel : ObservableObject {
	[ObservableProperty] private int m_graphLaneCount = 1;

	[ObservableProperty] private bool m_isCurrent;
	[ObservableProperty] private bool m_isOnCurrentBranch;
	[ObservableProperty] private int m_lane;

	public HistoryRowViewModel(WorkspaceHistoryRevision revision, HistoryViewModel owner) {
		Owner = owner;
		Id = revision.Id;
		Sequence = revision.Sequence;
		Parents = revision.Parents.ToArray();
		NodeUid = revision.NodeUid;
		NodeName = revision.NodeName;
		Operation = revision.Operation;
		Subject = revision.Subject;
		PreviousValue = revision.PreviousValue;
		CurrentValue = revision.CurrentValue;
		Icon = IconFor(Operation);
		OperationText = DisplayText(Operation, Subject);
	}

	public HistoryViewModel Owner { get; }
	public ulong Id { get; }
	public ulong Sequence { get; }
	public IReadOnlyList<ulong> Parents { get; }
	public string NodeUid { get; }
	public string NodeName { get; }
	public HistoryOperation Operation { get; }
	public string Subject { get; }
	public string OperationText { get; }
	public string PreviousValue { get; }
	public string CurrentValue { get; }
	public LucideIconKind Icon { get; }
	public bool HasRowAbove { get; internal set; }
	public bool HasRowBelow { get; internal set; }
	public bool IsAboveCurrent { get; internal set; }
	public IReadOnlyList<ulong> TopLanes { get; internal set; } = [];
	public IReadOnlyList<ulong> BottomLanes { get; internal set; } = [];
	public IReadOnlySet<ulong> ActiveLineage { get; internal set; } = new HashSet<ulong>();
	public string AccentColorKey => IsOnCurrentBranch ? "Red" : "TextMuted";
	public string TextColorKey => IsOnCurrentBranch ? "Text" : "TextMuted";

	partial void OnIsOnCurrentBranchChanged(bool value) {
		OnPropertyChanged(nameof(AccentColorKey));
		OnPropertyChanged(nameof(TextColorKey));
	}

	private static LucideIconKind IconFor(HistoryOperation operation) {
		return (int)operation switch {
			0 => LucideIconKind.History,
			1 => LucideIconKind.Plus,
			2 => LucideIconKind.Trash2,
			3 => LucideIconKind.Pencil,
			4 => LucideIconKind.ToggleRight,
			5 => LucideIconKind.PencilLine,
			6 => LucideIconKind.MoveVertical,
			7 => LucideIconKind.GitBranch,
			8 => LucideIconKind.Replace,
			9 => LucideIconKind.CopyPlus,
			10 => LucideIconKind.PackagePlus,
			11 => LucideIconKind.ClipboardPaste,
			12 => LucideIconKind.FileUp,
			13 => LucideIconKind.CirclePlay,
			14 => LucideIconKind.GitCommitHorizontal,
			15 => LucideIconKind.GitMerge,
			_ => LucideIconKind.CircleDot
		};
	}

	private static string LabelFor(HistoryOperation operation) {
		return (int)operation switch {
			0 => "Initial state",
			1 => "Created",
			2 => "Deleted",
			3 => "Renamed",
			4 => "Enabled",
			5 => "Value changed",
			6 => "Moved",
			7 => "Reparent",
			8 => "Change type",
			9 => "Duplicated",
			10 => "Spawn prefab",
			11 => "Pasted",
			12 => "Promote to prefab",
			13 => "Function invoked",
			14 => "Cherry-pick",
			15 => "Merge",
			_ => "Changed"
		};
	}

	private static string DisplayText(HistoryOperation operation, string subject) {
		if ((int)operation == 4) return "Enabled";
		if ((int)operation == 7) return "Reparent";
		if (string.IsNullOrWhiteSpace(subject)) return LabelFor(operation);
		if ((int)operation == 5 && subject.EndsWith(" changed", StringComparison.OrdinalIgnoreCase))
			return subject[..^" changed".Length];
		return subject;
	}
}

public partial class HistoryViewModel : Tool, IDisposable {
	private readonly Dictionary<ulong, HistoryRowViewModel> m_byId = [];

	[ObservableProperty] private bool m_available;
	[ObservableProperty] private ulong m_currentRevision;
	[ObservableProperty] private bool m_hasWorkspace;
	[ObservableProperty] private bool m_isDirty;
	[ObservableProperty] private HistoryRowViewModel? m_selectedRevision;
	private WorkspaceHistoryState? m_state;
	[ObservableProperty] private bool m_transactionOpen;
	[ObservableProperty] private string m_unavailableReason = "No workspace open";

	public ObservableCollection<HistoryRowViewModel> Rows { get; } = [];
	public ulong WorkspaceHandle => m_state?.WorkspaceHandle ?? 0;

	public void Dispose() {
		if (m_state is not null) m_state.Changed -= Apply;
		m_state = null;
		GC.SuppressFinalize(this);
	}

	public void SetWorkspace(WorkspaceHistoryState? state) {
		if (ReferenceEquals(m_state, state)) return;
		if (m_state is not null) m_state.Changed -= Apply;
		Clear();
		m_state = state;
		if (m_state is null) return;
		m_state.Changed += Apply;
		Apply();
	}

	public void Clear() {
		Rows.Clear();
		m_byId.Clear();
		HasWorkspace = false;
		Available = false;
		UnavailableReason = "No workspace open";
		CurrentRevision = 0;
		SelectedRevision = null;
		NotifyCommands();
	}

	private void Apply() {
		if (m_state is null) return;
		HasWorkspace = true;
		Available = m_state.Available;
		UnavailableReason = m_state.Available ? "" : m_state.UnavailableReason;
		IsDirty = m_state.IsDirty;
		TransactionOpen = m_state.TransactionOpen || m_state.IsBusy;
		CurrentRevision = m_state.CurrentRevision;

		Rows.Clear();
		m_byId.Clear();
		foreach (var revision in m_state.Revisions.OrderByDescending(r => r.Sequence)) {
			var row = new HistoryRowViewModel(revision, this);
			Rows.Add(row);
			m_byId[row.Id] = row;
		}

		var currentLineage = new HashSet<ulong>();
		for (var id = CurrentRevision; id != 0 && m_byId.TryGetValue(id, out var row);) {
			if (!currentLineage.Add(id) || row.Parents.Count == 0) break;
			id = row.Parents[0];
		}

		foreach (var row in Rows) {
			row.IsCurrent = row.Id == CurrentRevision;
			row.IsOnCurrentBranch = currentLineage.Contains(row.Id);
			row.ActiveLineage = currentLineage;
		}

		BuildGraphLanes();
		SelectedRevision = m_byId.GetValueOrDefault(CurrentRevision);
		NotifyCommands();
	}

	private void BuildGraphLanes() {
		var lanes = new List<ulong>();
		var max = 1;
		var currentIndex = m_byId.TryGetValue(CurrentRevision, out var currentRow)
			? Rows.IndexOf(currentRow)
			: -1;
		for (var rowIndex = 0; rowIndex < Rows.Count; rowIndex++) {
			var row = Rows[rowIndex];
			row.IsAboveCurrent = currentIndex >= 0 && rowIndex < currentIndex;
			var lane = lanes.IndexOf(row.Id);
			if (lane < 0) {
				lane = lanes.Count;
				lanes.Add(row.Id);
			}

			row.TopLanes = lanes.ToArray();
			row.Lane = lane;
			lanes.RemoveAt(lane);
			for (var i = 0; i < row.Parents.Count; i++) {
				var parent = row.Parents[i];
				var existing = lanes.IndexOf(parent);
				if (existing >= 0) continue;
				lanes.Insert(Math.Min(lane + i, lanes.Count), parent);
			}

			row.BottomLanes = lanes.ToArray();
			row.HasRowAbove = rowIndex > 0;
			row.HasRowBelow = rowIndex + 1 < Rows.Count;
			max = Math.Max(max, Math.Max(row.TopLanes.Count, row.BottomLanes.Count));
		}

		foreach (var row in Rows) row.GraphLaneCount = max;
	}

	partial void OnSelectedRevisionChanged(HistoryRowViewModel? value) {
		NotifyCommands();
	}

	private void NotifyCommands() {
		UndoCommand.NotifyCanExecuteChanged();
		RedoCommand.NotifyCanExecuteChanged();
		CheckoutCommand.NotifyCanExecuteChanged();
		CherryPickCommand.NotifyCanExecuteChanged();
		MergeCommand.NotifyCanExecuteChanged();
		CheckoutRevisionCommand.NotifyCanExecuteChanged();
		CherryPickRevisionCommand.NotifyCanExecuteChanged();
		MergeRevisionCommand.NotifyCanExecuteChanged();
	}

	private bool CanUseHistory() {
		return HasWorkspace && Available && !TransactionOpen;
	}

	private bool CanUndo() {
		return CanUseHistory() && m_byId.TryGetValue(CurrentRevision, out var current) && current.Parents.Count > 0;
	}

	private bool CanRedo() {
		return CanUseHistory() && Rows.Any(r => r.Parents.Contains(CurrentRevision));
	}

	private bool CanCheckout() {
		return CanUseHistory() && SelectedRevision is { } row && row.Id != CurrentRevision;
	}

	private bool CanCherryPick() {
		return CanUseHistory() && SelectedRevision is { } row &&
			row.Parents.Count == 1 && (int)row.Operation is not (0 or 15);
	}

	private bool CanMerge() {
		return CanUseHistory() && SelectedRevision is { } row && row.Id != CurrentRevision;
	}

	private bool CanCheckoutRevision(HistoryRowViewModel? row) {
		return CanUseHistory() && row is not null && row.Id != CurrentRevision;
	}

	private bool CanCherryPickRevision(HistoryRowViewModel? row) {
		return CanUseHistory() && row is not null &&
			row.Parents.Count == 1 && (int)row.Operation is not (0 or 15);
	}

	private bool CanMergeRevision(HistoryRowViewModel? row) {
		return CanUseHistory() && row is not null && row.Id != CurrentRevision;
	}

	[RelayCommand(CanExecute = nameof(CanUndo))]
	private void Undo() {
		m_state?.Undo();
	}

	[RelayCommand(CanExecute = nameof(CanRedo))]
	private void Redo() {
		m_state?.Redo();
	}

	[RelayCommand(CanExecute = nameof(CanCheckout))]
	private void Checkout() {
		if (SelectedRevision is { } row)
			m_state?.Checkout(row.Id);
	}

	[RelayCommand(CanExecute = nameof(CanCherryPick))]
	private void CherryPick() {
		if (SelectedRevision is { } row)
			m_state?.CherryPick(row.Id);
	}

	[RelayCommand(CanExecute = nameof(CanMerge))]
	private void Merge() {
		if (SelectedRevision is { } row)
			m_state?.Merge(row.Id);
	}

	[RelayCommand(CanExecute = nameof(CanCheckoutRevision))]
	private void CheckoutRevision(HistoryRowViewModel? row) {
		if (row is not null) m_state?.Checkout(row.Id);
	}

	[RelayCommand(CanExecute = nameof(CanCherryPickRevision))]
	private void CherryPickRevision(HistoryRowViewModel? row) {
		if (row is not null) m_state?.CherryPick(row.Id);
	}

	[RelayCommand(CanExecute = nameof(CanMergeRevision))]
	private void MergeRevision(HistoryRowViewModel? row) {
		if (row is not null) m_state?.Merge(row.Id);
	}

	public void Activate(HistoryRowViewModel row) {
		SelectedRevision = row;
		if (CheckoutCommand.CanExecute(null)) CheckoutCommand.Execute(null);
	}
}
