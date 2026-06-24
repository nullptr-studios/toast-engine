using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Components.Modals;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public class HierarchyElement {
	public HierarchyElement(Proto.Events.HierarchyElement e, HierarchyViewModel owner, HierarchyElement? parent = null) {
		Owner = owner;
		Parent = parent;
		Name = e.Name;
		Uid = e.Uid;
		Type = e.Type;
		Enabled = e.Enabled;
		foreach (var c in e.Children) {
			if (c is null) continue;
			Children.Add(new HierarchyElement(c, owner, this));
		}

		foreach (var child in Children) FilteredChildren.Add(child);
	}

	public string Name { get; set; }
	public string Uid { get; set; }
	public string Type { get; set; }
	public bool Enabled { get; set; }
	public string? Icon { get; set; }
	public string? Color { get; set; }
	public bool IsRoot { get; set; }
	public ObservableCollection<HierarchyElement> Children { get; set; } = [];
	public ObservableCollection<HierarchyElement> FilteredChildren { get; } = [];

	public HierarchyViewModel Owner { get; }
	public HierarchyElement? Parent { get; }

	// returns true if this element or any of its descendants match the query
	// rebuilds FilteredChildren so the tree view only shows visible nodes
	public bool ApplyFilter(string query) {
		FilteredChildren.Clear();

		if (string.IsNullOrEmpty(query)) {
			foreach (var child in Children) {
				child.ApplyFilter(query);
				FilteredChildren.Add(child);
			}

			return true;
		}

		foreach (var child in Children)
			if (child.ApplyFilter(query))
				FilteredChildren.Add(child);

		return Name.Contains(query, StringComparison.OrdinalIgnoreCase) || FilteredChildren.Count > 0;
	}
}

public partial class HierarchyViewModel : Tool {
	// "end" -> engine inserts the node after the last sibling
	private const string MoveToEnd = "end";

	// field not local -> GC would drop it and kill all the subscriptions
	// ReSharper disable once PrivateFieldCanBeConvertedToLocalVariable
	private readonly Listener m_listener;

	[ObservableProperty] private string m_filterText = "";

	[ObservableProperty] private HierarchyElement? m_selectedNode;

	public static HierarchyViewModel? Current { get; private set; }

	public event Action? HierarchyChanged;

	// raised whenever the selected node changes
	public static event Action<HierarchyElement?>? SelectionChanged;

	public HierarchyViewModel() {
		Current = this;
		m_listener = new Listener();

		// engine sends UpdateHierarchyData after every change (create, delete, move, rename...)
		// we post to the UI thread because engine callbacks come on the native tick thread
		m_listener.Subscribe<UpdateHierarchyData>(e => {
			Dispatcher.UIThread.Post(() => {
				var prevUid = SelectedNode?.Uid;
				Root.Clear();
				Root.Add(new HierarchyElement(e.Root, this) { IsRoot = true });
				// restore selection after the tree rebuilds so editing a node doesnt lose focus
				SelectedNode = prevUid is null ? null : Find(Root, prevUid);
				ActiveWorkspace?.SetRootNode(Root[0].Uid);
				ApplyFilterToRoot();
				HierarchyChanged?.Invoke();
			});
		});
	}

	public ObservableCollection<HierarchyElement> Root { get; } = [];

	private WorkspaceViewModel? ActiveWorkspace => Root.Count > 0 && Factory is DockFactory f ? f.ActiveWorkspace : null;

	// partial method hooked by the source generator -> fires when FilterText changes
	partial void OnFilterTextChanged(string value) {
		ApplyFilterToRoot();
	}

	partial void OnSelectedNodeChanged(HierarchyElement? value) {
		// tell the engine which node to stream inspector data for ("" clears the focus)
		Events.Send(new SetFocusedNode { Node = value?.Uid ?? "" });
		SelectionChanged?.Invoke(value);
	}

	private void ApplyFilterToRoot() {
		foreach (var root in Root) root.ApplyFilter(FilterText);
	}

	public void Clear() {
		Dispatcher.UIThread.Post(() => {
			Root.Clear();
			SelectedNode = null;
		});
	}

	public HierarchyElement? Find(string uid) => Find(Root, uid);

	private static HierarchyElement? Find(IEnumerable<HierarchyElement> elements, string uid) {
		foreach (var el in elements) {
			if (el.Uid == uid) return el;
			if (Find(el.Children, uid) is { } found) return found;
		}

		return null;
	}

	// explicit arg -> selected node -> root, fallback chain for all hierarchy commands
	private HierarchyElement? Target(HierarchyElement? target) {
		return target ?? SelectedNode ?? (Root.Count > 0 ? Root[0] : null);
	}

	[RelayCommand]
	private async Task AddNode(HierarchyElement? target) {
		var t = Target(target);
		if (t is null) return;

		if (App.MainWindow is not { } owner) return;
		var type = await new NodeTypePickerModal().ShowDialog<string?>(owner);
		if (type is null) return;

		Events.Send(new WorkspaceCreateNode { Parent = t.Uid, Type = type });
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
		var i = siblings.IndexOf(t);
		if (i <= 0) return;
		// predecessor is i-2 (the node before the one we're skipping over)
		// empty string means "insert at the start"
		var before = i - 2 >= 0 ? siblings[i - 2].Uid : "";
		Events.Send(new WorkspaceMoveNodeTo { Target = t.Uid, NewParent = parent.Uid, Predecessor = before });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private void MoveDown(HierarchyElement? target) {
		var t = Target(target);
		if (t?.Parent is not { } parent) return;
		var siblings = parent.Children;
		var i = siblings.IndexOf(t);
		if (i < 0 || i >= siblings.Count - 1) return;
		// move after i+1 (the node below us becomes our predecessor)
		Events.Send(new WorkspaceMoveNodeTo
			{ Target = t.Uid, NewParent = parent.Uid, Predecessor = siblings[i + 1].Uid });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private async Task ChangeParent(HierarchyElement? target) {
		var t = Target(target);
		if (t?.Parent is not { } prevParent) return;

		if (App.MainWindow is not { } owner) return;
		var w = new HierarchyPickerModal(Root, t);
		var newParent = await w.ShowDialog<string?>(owner);
		if (newParent is null || newParent == t.Uid || newParent == prevParent.Uid) return;

		Events.Send(new WorkspaceMoveNodeTo { Target = t.Uid, NewParent = newParent, Predecessor = MoveToEnd });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private void Promote(HierarchyElement? target) {
		// TODO: Promote the target node into its own Node file
	}

	[RelayCommand]
	private async Task Save(HierarchyElement? target) {
		if (ActiveWorkspace is { } ws) await ws.Save();
	}

	[RelayCommand]
	private async Task SaveAs(HierarchyElement? target) {
		if (ActiveWorkspace is { } ws) await ws.SaveAs();
	}

	[RelayCommand]
	private void Delete(HierarchyElement? target) {
		if (target is null || target == Root[0]) return;
		Events.Send(new WorkspaceRemoveNode { Target = target.Uid });
		WorkspaceState.MarkModified();
	}
}
