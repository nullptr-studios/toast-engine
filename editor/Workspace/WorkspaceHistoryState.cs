using System;
using System.Collections.Generic;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Engine;
using Google.Protobuf;
using Proto.Events;

namespace editor.Workspace;

public sealed class WorkspaceHistoryRevision {
	public required ulong Id { get; init; }
	public required ulong Sequence { get; init; }
	public required IReadOnlyList<ulong> Parents { get; init; }
	public required ByteString Snapshot { get; init; }
	public string NodeUid { get; init; } = "";
	public string NodeName { get; init; } = "";
	public HistoryOperation Operation { get; init; }
	public string Subject { get; init; } = "";
	public string PreviousValue { get; init; } = "";
	public string CurrentValue { get; init; } = "";
}

public partial class WorkspaceHistoryState : ObservableObject {
	private readonly Dictionary<ulong, WorkspaceHistoryRevision> m_revisions = [];

	[ObservableProperty] private bool m_available;
	[ObservableProperty] private ulong m_currentRevision;
	[ObservableProperty] private bool m_isBusy;
	[ObservableProperty] private bool m_isDirty;
	private ulong m_nextRequest = 1;
	private ulong m_nextRevision = 1;
	private ulong m_nextSequence = 1;
	private PendingApply? m_pendingApply;
	private PendingMerge? m_pendingMerge;
	private ByteString? m_savedSnapshot;
	[ObservableProperty] private bool m_transactionOpen;
	[ObservableProperty] private string m_unavailableReason = "Loading history…";

	public WorkspaceHistoryState(ulong workspaceHandle) {
		WorkspaceHandle = workspaceHandle;
	}

	public ulong WorkspaceHandle { get; }
	public IReadOnlyCollection<WorkspaceHistoryRevision> Revisions => m_revisions.Values;
	public event Action? Changed;

	public void RequestInitial() {
		Events.Send(new RequestWorkspaceHistory { WorkspaceHandle = WorkspaceHandle });
	}

	public void AcceptInitial(WorkspaceHistoryInitialSnapshot initial) {
		if (initial.WorkspaceHandle != WorkspaceHandle || m_revisions.Count != 0) return;
		Available = initial.Available;
		UnavailableReason = initial.Available ? "" : initial.UnavailableReason;
		if (!initial.Available || initial.Snapshot.IsEmpty) {
			Changed?.Invoke();
			return;
		}

		var revision = new WorkspaceHistoryRevision {
			Id = m_nextRevision++,
			Sequence = m_nextSequence++,
			Parents = [],
			Snapshot = initial.Snapshot,
			Operation = 0,
			Subject = "Initial state"
		};
		m_revisions.Add(revision.Id, revision);
		CurrentRevision = revision.Id;
		m_savedSnapshot = initial.InitiallySaved ? revision.Snapshot : null;
		RefreshDirty();
		Changed?.Invoke();
	}

	public void AcceptCommit(WorkspaceHistoryCommitted commit) {
		if (commit.WorkspaceHandle != WorkspaceHandle || !Available || IsBusy) return;
		if (!m_revisions.TryGetValue(CurrentRevision, out var current)) return;
		if (!current.Snapshot.Equals(commit.BeforeSnapshot)) {
			Available = false;
			UnavailableReason = "History lost synchronization with the workspace";
			Changed?.Invoke();
			return;
		}

		var revision = new WorkspaceHistoryRevision {
			Id = m_nextRevision++,
			Sequence = m_nextSequence++,
			Parents = [CurrentRevision],
			Snapshot = commit.AfterSnapshot,
			NodeUid = commit.NodeUid,
			NodeName = commit.NodeName,
			Operation = commit.Operation,
			Subject = commit.Subject,
			PreviousValue = commit.PreviousValue,
			CurrentValue = commit.CurrentValue
		};
		m_revisions.Add(revision.Id, revision);
		CurrentRevision = revision.Id;
		RefreshDirty();
		Changed?.Invoke();
	}

	public void SetTransactionOpen(bool open) {
		if (TransactionOpen == open) return;
		TransactionOpen = open;
		Changed?.Invoke();
	}

	public void MarkSaved(ByteString canonicalSnapshot) {
		m_savedSnapshot = canonicalSnapshot;
		RefreshDirty();
		Changed?.Invoke();
	}

	public bool Undo() {
		if (!CanNavigate() || !m_revisions.TryGetValue(CurrentRevision, out var current) || current.Parents.Count == 0)
			return false;
		return ApplyExisting(current.Parents[0]);
	}

	public bool Redo() {
		if (!CanNavigate()) return false;
		var child = m_revisions.Values
			.Where(r => r.Parents.Contains(CurrentRevision))
			.MaxBy(r => r.Sequence);
		return child is not null && ApplyExisting(child.Id);
	}

	public bool Checkout(ulong revision) {
		return revision != CurrentRevision && CanNavigate() && ApplyExisting(revision);
	}

	public bool CherryPick(ulong revision) {
		if (!CanNavigate() || !m_revisions.TryGetValue(revision, out var source) || source.Parents.Count != 1 ||
		    (int)source.Operation is 0 or 15) return false;
		return PrepareMerge(source.Parents[0], source.Id, false);
	}

	public bool Merge(ulong revision) {
		if (!CanNavigate() || revision == CurrentRevision || !m_revisions.ContainsKey(revision)) return false;
		var @base = CommonAncestor(CurrentRevision, revision);
		return @base != 0 && @base != revision && PrepareMerge(@base, revision, true);
	}

	private bool CanNavigate() {
		return Available && !TransactionOpen && !IsBusy && m_revisions.ContainsKey(CurrentRevision);
	}

	private bool ApplyExisting(ulong revision) {
		if (!m_revisions.TryGetValue(revision, out var target)) return false;
		var request = m_nextRequest++;
		m_pendingApply = new PendingApply(request, revision, null, default, "", null);
		IsBusy = true;
		Changed?.Invoke();
		Events.Send(new WorkspaceApplyHistorySnapshot {
			WorkspaceHandle = WorkspaceHandle,
			Request = request,
			Snapshot = target.Snapshot
		});
		return true;
	}

	private bool PrepareMerge(ulong baseRevision, ulong sourceRevision, bool isMerge) {
		if (!m_revisions.TryGetValue(baseRevision, out var @base) ||
		    !m_revisions.TryGetValue(CurrentRevision, out var current) ||
		    !m_revisions.TryGetValue(sourceRevision, out var source)) return false;
		var request = m_nextRequest++;
		m_pendingMerge = new PendingMerge(request, sourceRevision, isMerge);
		IsBusy = true;
		Changed?.Invoke();
		Events.Send(new WorkspacePrepareHistoryMerge {
			WorkspaceHandle = WorkspaceHandle,
			Request = request,
			IsMerge = isMerge,
			BaseSnapshot = @base.Snapshot,
			CurrentSnapshot = current.Snapshot,
			IncomingSnapshot = source.Snapshot
		});
		return true;
	}

	public void AcceptMergePrepared(WorkspaceHistoryMergePrepared result) {
		if (result.WorkspaceHandle != WorkspaceHandle || m_pendingMerge is not { } pending ||
		    pending.Request != result.Request)
			return;
		m_pendingMerge = null;
		if (!result.Success || result.Snapshot.IsEmpty) {
			IsBusy = false;
			Changed?.Invoke();
			return;
		}

		if (m_revisions.TryGetValue(CurrentRevision, out var current) && current.Snapshot.Equals(result.Snapshot)) {
			IsBusy = false;
			Changed?.Invoke();
			return;
		}

		var parents = pending.IsMerge
			? new[] { CurrentRevision, pending.SourceRevision }
			: new[] { CurrentRevision };
		var request = m_nextRequest++;
		var operation = (HistoryOperation)(pending.IsMerge ? 15 : 14);
		m_pendingApply = new PendingApply(request, null, result.Snapshot, operation,
			pending.IsMerge ? "Merge" : "Cherry-pick", parents);
		Events.Send(new WorkspaceApplyHistorySnapshot {
			WorkspaceHandle = WorkspaceHandle,
			Request = request,
			Snapshot = result.Snapshot
		});
	}

	public void AcceptSnapshotApplied(WorkspaceHistorySnapshotApplied result) {
		if (result.WorkspaceHandle != WorkspaceHandle || m_pendingApply is not { } pending ||
		    pending.Request != result.Request)
			return;
		m_pendingApply = null;
		IsBusy = false;
		if (!result.Success) {
			Changed?.Invoke();
			return;
		}

		if (pending.ExistingRevision is { } existing) {
			CurrentRevision = existing;
		} else if (pending.Snapshot is { } snapshot && pending.Parents is { } parents) {
			var revision = new WorkspaceHistoryRevision {
				Id = m_nextRevision++,
				Sequence = m_nextSequence++,
				Parents = parents,
				Snapshot = snapshot,
				Operation = pending.Operation,
				Subject = pending.Subject
			};
			m_revisions.Add(revision.Id, revision);
			CurrentRevision = revision.Id;
		}

		RefreshDirty();
		Changed?.Invoke();
	}

	private ulong CommonAncestor(ulong left, ulong right) {
		var leftAncestors = Ancestors(left);
		return Ancestors(right)
			.Where(leftAncestors.Contains)
			.Select(id => m_revisions[id])
			.MaxBy(r => r.Sequence)?.Id ?? 0;
	}

	private HashSet<ulong> Ancestors(ulong revision) {
		var result = new HashSet<ulong>();
		var stack = new Stack<ulong>();
		stack.Push(revision);
		while (stack.TryPop(out var id) && result.Add(id) && m_revisions.TryGetValue(id, out var item))
			foreach (var parent in item.Parents)
				stack.Push(parent);
		return result;
	}

	private void RefreshDirty() {
		IsDirty = !m_revisions.TryGetValue(CurrentRevision, out var current) ||
			m_savedSnapshot is null || !current.Snapshot.Equals(m_savedSnapshot);
	}

	private sealed record PendingApply(
		ulong Request,
		ulong? ExistingRevision,
		ByteString? Snapshot,
		HistoryOperation Operation,
		string Subject,
		IReadOnlyList<ulong>? Parents);

	private sealed record PendingMerge(ulong Request, ulong SourceRevision, bool IsMerge);
}
