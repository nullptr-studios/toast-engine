using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.AssetBrowser;
using editor.Services;

namespace editor.Workspace;

public class HierarchyElement {
	public string Name {get; set;}
	public string Uid {get; set;}
	public bool Enabled {get; set;}
	public string? Icon {get; set;}
	public string? Color {get; set;}
	public bool IsRoot {get; set;}
	public ObservableCollection<HierarchyElement> Children { get; set; } = [];

	public HierarchyViewModel Owner { get; }

	public HierarchyElement? Parent { get; }

	public HierarchyElement(Proto.Events.HierarchyElement e, HierarchyViewModel owner, HierarchyElement? parent = null) {
		Owner = owner;
		Parent = parent;
		Name = e.Name;
		Uid = e.Uid;
		Enabled = e.Enabled;
		foreach (var c in e.Children) {
			if (c is null) continue;
			Children.Add(new HierarchyElement(c, owner, this));
		}
	}
}

public partial class HierarchyViewModel : Tool {
	private const string MoveToEnd = "end";

	// ReSharper disable once PrivateFieldCanBeConvertedToLocalVariable
	private readonly Listener m_listener;
	public ObservableCollection<HierarchyElement> Root { get; } = [];

	[ObservableProperty]
	private HierarchyElement? m_selectedNode;

	public HierarchyViewModel() {
		m_listener = new Listener();
		m_listener.Subscribe<Proto.Events.UpdateHierarchyData>(e => {
			Dispatcher.UIThread.Post(() => {
				var prevUid = SelectedNode?.Uid;
				Root.Clear();
				Root.Add(new HierarchyElement(e.Root, this) { IsRoot = true });
				SelectedNode = prevUid is null ? null : Find(Root, prevUid);
			});
		});
	}

	// Blank the hierarchy when no workspace is active
	public void Clear() {
		Dispatcher.UIThread.Post(() => {
			Root.Clear();
			SelectedNode = null;
		});
	}

	private static HierarchyElement? Find(IEnumerable<HierarchyElement> elements, string uid) {
		foreach (var el in elements) {
			if (el.Uid == uid) return el;
			if (Find(el.Children, uid) is { } found) return found;
		}
		return null;
	}

	private HierarchyElement? Target(HierarchyElement? target) =>
		target ?? SelectedNode ?? (Root.Count > 0 ? Root[0] : null);

	[RelayCommand]
	private async Task AddNode(HierarchyElement? target) {
		var t = Target(target);
		if (t is null) return;

		if (App.MainWindow is not { } owner) return;
		var w = new NewNodeView();
		var type = await w.ShowDialog<string?>(owner);
		if (type is null) return;

		Events.Send(new Proto.Events.WorkspaceCreateNode { Parent = t.Uid, Type = type });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private void LoadNode(HierarchyElement? target) {
		// TODO: Load a node from a file and add it as a child of Target(target)
	}

	[RelayCommand]
	private void Cut(HierarchyElement? target) {
		// TODO: Cut the target node onto the clipboard
	}

	[RelayCommand]
	private void Copy(HierarchyElement? target) {
		// TODO: Copy the target node onto the clipboard
	}

	[RelayCommand]
	private void Paste(HierarchyElement? target) {
		// TODO: Paste the clipboard node as a child of the target node
	}

	[RelayCommand]
	private void Rename(HierarchyElement? target) {
		// TODO: Rename the target node
	}

	[RelayCommand]
	private void ChangeType(HierarchyElement? target) {
		// TODO: Change the type of the target node
	}

	[RelayCommand]
	private void Duplicate(HierarchyElement? target) {
		// TODO: Duplicate the target node in place
	}

	[RelayCommand]
	private void MoveUp(HierarchyElement? target) {
		var t = Target(target);
		if (t?.Parent is not { } parent) return;
		var siblings = parent.Children;
		int i = siblings.IndexOf(t);
		if (i <= 0) return;
		var before = i - 2 >= 0 ? siblings[i - 2].Uid : "";
		Events.Send(new Proto.Events.WorkspaceMoveNodeTo { Target = t.Uid, NewParent = parent.Uid, Predecessor = before });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private void MoveDown(HierarchyElement? target) {
		var t = Target(target);
		if (t?.Parent is not { } parent) return;
		var siblings = parent.Children;
		int i = siblings.IndexOf(t);
		if (i < 0 || i >= siblings.Count - 1) return;        // already last
		// new index i+1: predecessor is the node currently at i+1
		Events.Send(new Proto.Events.WorkspaceMoveNodeTo { Target = t.Uid, NewParent = parent.Uid, Predecessor = siblings[i + 1].Uid });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private async Task ChangeParent(HierarchyElement? target) {
		var t = Target(target);
		if (t?.Parent is not { } prevParent) return;

		if (App.MainWindow is not { } owner) return;
		var w = new HierarchySelect(Root, exclude: t);
		var newParent = await w.ShowDialog<string?>(owner);
		if (newParent is null || newParent == t.Uid || newParent == prevParent.Uid) return;

		Events.Send(new Proto.Events.WorkspaceMoveNodeTo { Target = t.Uid, NewParent = newParent, Predecessor = MoveToEnd });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private void Promote(HierarchyElement? target) {
		// TODO: Promote the target node into its own Node file
	}

	private ViewportViewModel? ActiveWorkspace =>
		Root.Count > 0 && Factory is DockFactory f ? f.ActiveViewport : null;

	[RelayCommand]
	private async Task Save(HierarchyElement? target) {
		if (ActiveWorkspace is { } ws) await ws.Save(Root[0]);
	}

	[RelayCommand]
	private async Task SaveAs(HierarchyElement? target) {
		if (ActiveWorkspace is { } ws) await ws.SaveAs(Root[0]);
	}

	[RelayCommand]
	private void Delete(HierarchyElement? target) {
		if (target is null || target == Root[0]) return;
		Events.Send(new Proto.Events.WorkspaceRemoveNode { Target = target.Uid });
		WorkspaceState.MarkModified();
	}
}
