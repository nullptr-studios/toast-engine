using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Mvvm.Controls;
using editor.Assets;
using editor.Components.Modals;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public class HierarchyElement : INotifyPropertyChanged {
	private string m_draftName = "";

	private bool m_isDropTarget;

	private bool m_isExpanded = true;

	private bool m_isRenaming;

	private bool m_isSelected;

	public HierarchyElement(Proto.Events.HierarchyElement e, HierarchyViewModel owner, HierarchyElement? parent = null) {
		Owner = owner;
		Parent = parent;
		Name = e.Name;
		Uid = e.Uid;
		Type = e.Type;
		IsPrefab = e.IsPrefab;
		Enabled = e.Enabled;
		m_isExpanded = !owner.IsCollapsed(e.Uid); // restore persisted fold state
		Color = ReflectionDatabase.ResolveColor(e.Type);
		foreach (var c in e.Children) {
			if (c is null) continue;
			Children.Add(new HierarchyElement(c, owner, this));
		}

		try {
			var iconName = ReflectionDatabase.ResolveIcon(e.Type);
			Icon = new Bitmap(AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/1.5x/{iconName}.png")));
			SmallIcon = new Bitmap(AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/1x/{iconName}.png")));
			LargeIcon = new Bitmap(AssetLoader.Open(new Uri($"avares://editor/Resources/node_icons/2x/{iconName}.png")));
		} catch (Exception ex) {
			Log.Warn($"Failed to open icon for {e.Type}: {ex.Message}");
		}

		foreach (var child in Children) FilteredChildren.Add(child);
	}

	public string Name { get; set; }
	public string Uid { get; set; }
	public string Type { get; set; }
	public bool Enabled { get; set; }
	public Bitmap? Icon { get; set; }      // On avares://editor/Resources/node_icons/1.5x/<Icon Attribute>.png
	public Bitmap? SmallIcon { get; set; } // On avares://editor/Resources/node_icons/1x/<Icon Attribute>.png
	public Bitmap? LargeIcon { get; set; } // On avares://editor/Resources/node_icons/2x/<Icon Attribute>.png
	public string? Color { get; set; }
	public bool IsRoot { get; set; }
	public bool IsPrefab { get; set; }
	public ObservableCollection<HierarchyElement> Children { get; set; } = [];
	public ObservableCollection<HierarchyElement> FilteredChildren { get; } = [];

	public HierarchyViewModel Owner { get; }
	public HierarchyElement? Parent { get; }

	public bool IsInsidePrefab => Parent?.IsPrefab == true || Parent?.IsInsidePrefab == true;
	public bool CanAddChildren => !IsPrefab && !IsInsidePrefab;

	public int Depth { get; set; }
	public int Index { get; set; }

	public Thickness ContentMargin =>
		new(HierarchyConnectorLayer.LeftPad + Depth * HierarchyConnectorLayer.IndentStep, 0, 0, 0);

	public bool IsExpanded {
		get => m_isExpanded;
		set {
			if (m_isExpanded == value) return;
			m_isExpanded = value;
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsExpanded)));
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsFolded)));
		}
	}

	public bool IsFolded => !IsExpanded && Children.Count > 0 && string.IsNullOrEmpty(Owner.FilterText);

	public bool IsSelected {
		get => m_isSelected;
		set => SetField(ref m_isSelected, value);
	}

	public bool IsDropTarget {
		get => m_isDropTarget;
		set => SetField(ref m_isDropTarget, value);
	}

	public bool IsRenaming {
		get => m_isRenaming;
		set => SetField(ref m_isRenaming, value);
	}

	public string DraftName {
		get => m_draftName;
		set {
			if (m_draftName == value) return;
			m_draftName = value;
			PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(DraftName)));
		}
	}

	public event PropertyChangedEventHandler? PropertyChanged;

	private void SetField(ref bool field, bool value, [CallerMemberName] string? name = null) {
		if (field == value) return;
		field = value;
		PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
	}

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

	private readonly Listener m_listener;
	private bool m_clipboardIsCut;

	private string? m_clipboardUid;

	[ObservableProperty] private string m_filterText = "";

	private HierarchyState? m_hierState;

	private bool m_pendingRenameAfterUpdate;
	private HashSet<string>? m_rowUidsSnapshot;

	[ObservableProperty] private HierarchyElement? m_selectedNode;

	[ObservableProperty] private bool m_showPrefabChildren;

	public HierarchyViewModel() {
		Current = this;
		m_listener = new Listener();
		Root.CollectionChanged += (_, _) => OnPropertyChanged(nameof(HasNodes));

		WorkspaceViewModel.PlayModeChanged += () => {
			SaveCommand.NotifyCanExecuteChanged();
			SaveAsCommand.NotifyCanExecuteChanged();
		};

		// engine sends UpdateHierarchyData after every change (create, delete, move, rename...)
		// we post to the UI thread because engine callbacks come on the native tick thread
		m_listener.Subscribe<UpdateHierarchyData>(e => {
			Dispatcher.UIThread.Post(() => {
				var prevUid = SelectedNode?.Uid;
				Root.Clear();
				if (!e.IsEmpty) {
					m_hierState = HierarchyState.Load(e.Root.Uid);
					Root.Add(new HierarchyElement(e.Root, this) { IsRoot = true });
				}

				// restore selection after the tree rebuilds so editing a node doesnt lose focus
				SelectedNode = prevUid is null ? null : Find(Root, prevUid);
				if (Root.Count > 0) ActiveWorkspace?.SetRootNode(Root[0].Uid);
				ApplyFilterToRoot();
				RebuildRows();
				HierarchyChanged?.Invoke();

				if (m_pendingRenameAfterUpdate && m_rowUidsSnapshot is not null) {
					m_pendingRenameAfterUpdate = false;
					var oldUids = m_rowUidsSnapshot;
					m_rowUidsSnapshot = null;
					var newNode = Rows.FirstOrDefault(r => !oldUids.Contains(r.Uid) && !r.IsRoot);
					if (newNode is not null) {
						SelectedNode = newNode;
						newNode.DraftName = newNode.Name;
						newNode.IsRenaming = true;
						RenameStarted?.Invoke(newNode);
					}
				}
			});
		});
	}

	public static HierarchyViewModel? Current { get; private set; }

	public ObservableCollection<HierarchyElement> Root { get; } = [];

	public bool HasNodes => Root.Count > 0;

	public ObservableCollection<HierarchyElement> Rows { get; } = [];

	private WorkspaceViewModel? ActiveWorkspace => Root.Count > 0 && Factory is DockFactory f ? f.ActiveWorkspace : null;

	public event Action? HierarchyChanged;
	public event Action<HierarchyElement>? RenameStarted;

	// raised whenever the selected node changes
	public static event Action<HierarchyElement?>? SelectionChanged;

	// partial method hooked by the source generator -> fires when FilterText changes
	partial void OnFilterTextChanged(string value) {
		// filtering drops the current selection so the search isn't anchored to a node
		if (!string.IsNullOrEmpty(value)) SelectedNode = null;
		ApplyFilterToRoot();
		RebuildRows();
	}

	partial void OnShowPrefabChildrenChanged(bool value) {
		RebuildRows();
		HierarchyChanged?.Invoke();
	}

	internal bool IsCollapsed(string uid) {
		return m_hierState?.Get(uid, false) ?? false;
	}

	public void RebuildRows() {
		Rows.Clear();
		var filtering = !string.IsNullOrEmpty(FilterText);
		foreach (var root in Root) Flatten(root, 0, filtering);
	}

	private void Flatten(HierarchyElement node, int depth, bool filtering) {
		node.Depth = depth;
		node.Index = Rows.Count;
		Rows.Add(node);
		// stop recursing into prefab children unless the toggle is on
		if (node.IsPrefab && !ShowPrefabChildren) return;
		if (!filtering && !node.IsExpanded) return;
		foreach (var child in node.FilteredChildren) Flatten(child, depth + 1, filtering);
	}

	public void ToggleExpand(HierarchyElement node) {
		node.IsExpanded = !node.IsExpanded;
		m_hierState?.Set(node.Uid, !node.IsExpanded); // store collapsed=true, expanded=false
		RebuildRows();
		HierarchyChanged?.Invoke();
	}

	[RelayCommand]
	private void ExpandAll() {
		foreach (var root in Root) SetSubtreeExpanded(root, true);
		RebuildRows();
		HierarchyChanged?.Invoke();
	}

	[RelayCommand]
	private void CollapseAll() {
		foreach (var root in Root) SetSubtreeExpanded(root, false);
		if (SelectedNode is { } sel) {
			SetSubtreeExpanded(sel, true);
			for (var p = sel.Parent; p is not null; p = p.Parent) SetExpanded(p, true);
		}

		RebuildRows();
		HierarchyChanged?.Invoke();
	}

	private void SetSubtreeExpanded(HierarchyElement node, bool expanded) {
		SetExpanded(node, expanded);
		foreach (var c in node.Children) SetSubtreeExpanded(c, expanded);
	}

	private void SetExpanded(HierarchyElement node, bool expanded) {
		node.IsExpanded = expanded;
		m_hierState?.Set(node.Uid, !expanded);
	}

	public static bool IsSelfOrDescendant(HierarchyElement node, HierarchyElement candidate) {
		if (ReferenceEquals(node, candidate)) return true;
		foreach (var c in node.Children)
			if (IsSelfOrDescendant(c, candidate))
				return true;
		return false;
	}

	public void DropInto(HierarchyElement dragged, HierarchyElement target) {
		if (dragged.Uid == target.Uid) return;
		Events.Send(new WorkspaceMoveNodeTo { Target = dragged.Uid, NewParent = target.Uid, Predecessor = MoveToEnd });
		WorkspaceState.MarkModified();
	}

	public void DropBeside(HierarchyElement dragged, HierarchyElement target, bool after) {
		if (target.Parent is not { } parent) return; // root has no siblings
		var siblings = parent.Children;
		var idx = siblings.IndexOf(target);
		if (idx < 0) return;
		var predecessor = after ? target.Uid : idx - 1 >= 0 ? siblings[idx - 1].Uid : "";
		if (predecessor == dragged.Uid) return; // already in place
		Events.Send(new WorkspaceMoveNodeTo { Target = dragged.Uid, NewParent = parent.Uid, Predecessor = predecessor });
		WorkspaceState.MarkModified();
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

	public HierarchyElement? Find(string uid) {
		return Find(Root, uid);
	}

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
		if (t is null || t.IsPrefab || t.IsInsidePrefab) return;

		if (App.MainWindow is not { } owner) return;
		var type = await new NodeTypeTree().ShowDialog<string?>(owner);
		if (type is null) return;

		Events.Send(new WorkspaceCreateNode { Parent = t.Uid, Type = type });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private async Task LoadNode(HierarchyElement? target) {
		var t = Target(target);
		if (t is null || t.IsPrefab || t.IsInsidePrefab) return;

		if (App.MainWindow is not { } owner) return;
		var uid = await new AssetList("Node").ShowDialog<string?>(owner);
		if (uid is null) return;

		Events.Send(new WorkspaceSpawn { Parent = t.Uid, IsUri = false, Uid = uid });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private void Cut(HierarchyElement? target) {
		var t = Target(target);
		if (t is null || t.IsRoot || t.IsInsidePrefab) return;
		m_clipboardUid = t.Uid;
		m_clipboardIsCut = true;
		Events.Send(new WorkspaceCopyNode { Source = t.Uid });
	}

	[RelayCommand]
	private void Copy(HierarchyElement? target) {
		var t = Target(target);
		if (t is null || t.IsInsidePrefab) return;
		m_clipboardUid = t.Uid;
		m_clipboardIsCut = false;
		Events.Send(new WorkspaceCopyNode { Source = t.Uid });
	}

	[RelayCommand]
	private void Paste(HierarchyElement? target) {
		if (m_clipboardUid is null) return;
		var t = Target(target);
		if (t is null || t.IsPrefab || t.IsInsidePrefab) return;

		m_rowUidsSnapshot = new HashSet<string>(Rows.Select(r => r.Uid));
		m_pendingRenameAfterUpdate = true;
		Events.Send(new WorkspacePasteNode { Parent = t.Uid });
		if (m_clipboardIsCut) {
			Events.Send(new WorkspaceRemoveNode { Target = m_clipboardUid });
			m_clipboardUid = null;
			m_clipboardIsCut = false;
		}

		WorkspaceState.MarkModified();
	}

	[RelayCommand(CanExecute = nameof(CanRename))]
	private void Rename(HierarchyElement? target) {
		var t = Target(target);
		if (t is null) return;
		t.DraftName = t.Name;
		t.IsRenaming = true;
		RenameStarted?.Invoke(t);
	}

	private bool CanRename(HierarchyElement? target) {
		var t = target ?? SelectedNode;
		return t is null || !t.IsRoot;
	}

	[RelayCommand]
	private async Task ChangeType(HierarchyElement? target) {
		var t = Target(target);
		if (t is null || t.IsPrefab || t.IsInsidePrefab) return;

		if (App.MainWindow is not { } owner) return;
		var type = await new NodeTypeTree().ShowDialog<string?>(owner);
		if (type is null) return;

		Events.Send(new NodeChangeType { Node = t.Uid, Type = type });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private void Duplicate(HierarchyElement? target) {
		var t = Target(target);
		if (t is null || t.IsRoot || t.IsInsidePrefab) return;
		m_rowUidsSnapshot = new HashSet<string>(Rows.Select(r => r.Uid));
		m_pendingRenameAfterUpdate = true;
		Events.Send(new WorkspaceDuplicateNode { Source = t.Uid, Parent = t.Parent?.Uid ?? "" });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private void MoveUp(HierarchyElement? target) {
		var t = Target(target);
		if (t?.Parent is not { } parent || t.IsInsidePrefab) return;
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
		if (t?.Parent is not { } parent || t.IsInsidePrefab) return;
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
		if (t?.Parent is not { } prevParent || t.IsInsidePrefab) return;

		if (App.MainWindow is not { } owner) return;
		var w = new HierarchyTree(Root, t);
		var newParent = await w.ShowDialog<string?>(owner);
		if (newParent is null || newParent == t.Uid || newParent == prevParent.Uid) return;

		Events.Send(new WorkspaceMoveNodeTo { Target = t.Uid, NewParent = newParent, Predecessor = MoveToEnd });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private async Task Promote(HierarchyElement? target) {
		var t = Target(target);
		if (t is null || t.IsRoot || t.IsPrefab || t.IsInsidePrefab) return;

		var virtualPath = await App.Modals.ShowSaveFile(t.Name);
		if (virtualPath is null) return;

		var realPath = ProjectContext.Resolve(virtualPath);
		Directory.CreateDirectory(Path.GetDirectoryName(realPath)!);
		File.WriteAllBytes(realPath, Array.Empty<byte>());
		var uid = UidGenerator.Generate();
		MetaFile.Write(realPath, new MetaHeader { Uid = uid, Type = "node" });
		AssetDatabase.RebuildAssetDatabase();
		Events.Send(new ReloadAssetsManifest());
		Events.Send(new WorkspacePromoteNode { Target = t.Uid, Path = virtualPath });
		WorkspaceState.MarkModified();
	}

	[RelayCommand]
	private void ToggleEnabled(HierarchyElement? target) {
		var t = target ?? SelectedNode;
		if (t is null || t.IsInsidePrefab) return;
		Events.Send(new NodeEnabled { Node = t.Uid, Enabled = !t.Enabled });
		WorkspaceState.MarkModified();
	}

	// saving is locked while any tab is in play mode
	private static bool CanSave() {
		return !WorkspaceViewModel.AnyPlayActive;
	}

	[RelayCommand(CanExecute = nameof(CanSave))]
	private async Task Save(HierarchyElement? target) {
		if (ActiveWorkspace is { } ws) await ws.Save();
	}

	[RelayCommand(CanExecute = nameof(CanSave))]
	private async Task SaveAs(HierarchyElement? target) {
		if (ActiveWorkspace is { } ws) await ws.SaveAs();
	}

	[RelayCommand]
	private void Delete(HierarchyElement? target) {
		if (Rows.Any(r => r.IsRenaming)) return;
		if (target is null || Root.Count == 0 || target == Root[0] || target.IsInsidePrefab) return;
		Events.Send(new WorkspaceRemoveNode { Target = target.Uid });
		WorkspaceState.MarkModified();
	}
}
