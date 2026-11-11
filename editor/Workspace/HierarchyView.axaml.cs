//
// Hierarchy.axaml.cs by Xein
// 4 Jun 2026
//

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Avalonia.VisualTree;
using editor.Components.Elements;
using editor.Engine;
using Proto.Events;

namespace editor.Workspace;

public partial class HierarchyView : UserControl, IWorkspaceShortcutTarget {
	private const double DragThreshold = 4;

	private static readonly HashSet<string> s_reservedNames = ["root", "world", "global"];

	private HierarchyElement? m_dropTarget;
	private PointerPressedEventArgs? m_pressArgs;

	private HierarchyElement? m_pressItem;
	private Point m_pressPoint;
	private bool m_pressWasSelected;
	private HierarchyViewModel? m_vm;
	private HierarchyElement? m_hoveredItem;
	private DispatcherTimer? m_expansionTimer;
	private Action? m_finishExpansion;

	public HierarchyView() {
		InitializeComponent();

		Rows.AddHandler(DragDrop.DragOverEvent, OnDragOver);
		Rows.AddHandler(DragDrop.DropEvent, OnDrop);
		Rows.AddHandler(DragDrop.DragLeaveEvent, (_, _) => ClearDropFeedback());
	}

	private HierarchyViewModel? Vm => DataContext as HierarchyViewModel;

	protected override void OnDataContextChanged(EventArgs e) {
		CompleteExpansion();
		base.OnDataContextChanged(e);
		if (m_vm is not null) {
			m_vm.HierarchyChanged -= RefreshConnector;
			m_vm.PropertyChanged -= OnVmPropertyChanged;
			m_vm.RenameStarted -= OnRenameStarted;
		}

		m_vm = DataContext as HierarchyViewModel;
		m_hoveredItem = null;
		if (m_vm is not null) {
			m_vm.HierarchyChanged += RefreshConnector;
			m_vm.PropertyChanged += OnVmPropertyChanged;
			m_vm.RenameStarted += OnRenameStarted;
		}

		RefreshConnector();
	}

	private void OnVmPropertyChanged(object? sender, PropertyChangedEventArgs e) {
		if (e.PropertyName == nameof(HierarchyViewModel.SelectedNode)) RefreshConnector();
	}

	private void RefreshConnector() {
		ClearDropFeedback();
		if (Vm is not { } vm) return;
		Connector.Rows = vm.Rows;
		var selected = -1;
		for (var i = 0; i < vm.Rows.Count; i++) {
			var el = vm.Rows[i];
			var isSel = ReferenceEquals(el, vm.SelectedNode);
			el.IsSelected = isSel;
			if (isSel) selected = i;
		}

		Connector.SelectedIndex = selected;
		Connector.HoveredIndex = m_hoveredItem is null ? -1 : vm.Rows.IndexOf(m_hoveredItem);
	}

	private void OnRowPointerEntered(object? sender, PointerEventArgs e) {
		if (sender is Control { DataContext: HierarchyElement node }) {
			m_hoveredItem = node; Connector.HoveredIndex = node.Index;
		}
	}
	private void OnRowPointerExited(object? sender, PointerEventArgs e) {
		if (sender is Control { DataContext: HierarchyElement node } && ReferenceEquals(node, m_hoveredItem)) {
			m_hoveredItem = null; Connector.HoveredIndex = -1;
		}
	}

	private void CompleteExpansion() {
		m_expansionTimer?.Stop(); m_expansionTimer = null;
		var finish = m_finishExpansion; m_finishExpansion = null; finish?.Invoke();
	}
	private void AnimateToggle(HierarchyElement node) {
		CompleteExpansion();
		if (Vm is not { } vm) return;
		if (!string.IsNullOrEmpty(vm.FilterText)) { vm.ToggleExpand(node); return; }
		var collapsing = node.IsExpanded;
		if (!collapsing) vm.ToggleExpand(node);
		var descendants = vm.Rows.Skip(node.Index + 1).TakeWhile(n => n.Depth > node.Depth).ToArray();
		if (descendants.Length == 0) { if (collapsing) vm.ToggleExpand(node); return; }
		foreach (var child in descendants) child.VisualHeight = collapsing ? HierarchyConnectorLayer.RowHeight : 0;
		m_finishExpansion = () => {
			foreach (var child in descendants) child.VisualHeight = HierarchyConnectorLayer.RowHeight;
			if (collapsing) vm.ToggleExpand(node);
			Connector.RefreshGeometry();
		};
		var clock = Stopwatch.StartNew();
		var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(8) };
		m_expansionTimer = timer;
		timer.Tick += (_, _) => {
			var t = Math.Min(clock.Elapsed.TotalMilliseconds / 180, 1);
			var progress = 1 - Math.Pow(1 - t, 3);
			foreach (var child in descendants) child.VisualHeight = HierarchyConnectorLayer.RowHeight * (collapsing ? 1 - progress : progress);
			Connector.RefreshGeometry();
			if (t >= 1) CompleteExpansion();
		};
		Connector.RefreshGeometry(); timer.Start();
	}
	protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e) {
		CompleteExpansion(); base.OnDetachedFromVisualTree(e);
	}

	private void OnEmptySpaceMenuOpening(object? sender, CancelEventArgs e) {
		if (Vm is { } vm) vm.SelectedNode = null;
	}

	private void OnItemPointerPressed(object? sender, PointerPressedEventArgs e) {
		if (m_expansionTimer is not null) CompleteExpansion();
		if (sender is not Control { DataContext: HierarchyElement el } || Vm is not { } vm) return;
		if (!vm.Rows.Contains(el)) return;
		if (el.IsInsidePrefab) return; // prefab interior nodes are read-only

		var props = e.GetCurrentPoint(this).Properties;

		if (props.IsRightButtonPressed) {
			// right-click selects then opens menu
			vm.SelectedNode = el;
			Focus();
			return;
		}

		if (!props.IsLeftButtonPressed) return;

		// remember the press so a small move can promote to a drag, and a click
		// can fall through to fold/unfold on release
		m_pressItem = el;
		m_pressArgs = e;
		m_pressPoint = e.GetPosition(this);
		m_pressWasSelected = ReferenceEquals(el, vm.SelectedNode);
		if (!m_pressWasSelected) vm.SelectedNode = el;
		Focus();
	}

	private async void OnItemPointerMoved(object? sender, PointerEventArgs e) {
		if (m_pressItem is null || m_pressArgs is null) return;
		if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;

		var delta = e.GetPosition(this) - m_pressPoint;
		if (delta.X * delta.X + delta.Y * delta.Y < DragThreshold * DragThreshold) return;

		var item = m_pressItem;
		var args = m_pressArgs;
		ClearPress();
		if (item.IsRoot) return; // the root cannot be reparented

		var data = new DataTransfer();
		data.Add(DataTransferItem.Create(NodeDragData.Format, item));
		await DragDrop.DoDragDropAsync(args, data, DragDropEffects.Copy | DragDropEffects.Move);
		SetDropTarget(null);
		Connector.SetDropLine(-1, 0);
	}

	private void OnItemPointerReleased(object? sender, PointerReleasedEventArgs e) {
		// a click on the already-selected node toggles fold
		if (m_pressItem is { } item && m_pressWasSelected) AnimateToggle(item);
		ClearPress();
	}

	private void ClearPress() {
		m_pressItem = null;
		m_pressArgs = null;
		m_pressWasSelected = false;
	}

	private void OnRenameStarted(HierarchyElement element) {
		Rows.ScrollIntoView(element);
		// delay one render frame so the TextBox is visible before we try to focus it
		Dispatcher.UIThread.Post(() => {
			if (Rows.ItemsPanelRoot is not Panel panel) return;
			foreach (var child in panel.Children) {
				if (child.DataContext != element) continue;
				var textBox = child.FindDescendantOfType<TextBox>();
				if (textBox is not null) {
					textBox.Focus();
					textBox.SelectAll();
				}

				break;
			}
		}, DispatcherPriority.Render);
	}

	private void OnRenameKeyDown(object? sender, KeyEventArgs e) {
		if (sender is not TextBox { DataContext: HierarchyElement el }) return;
		if (e.Key == Key.Enter) {
			e.Handled = true;
			CommitRename(el);
		} else if (e.Key == Key.Escape) {
			e.Handled = true;
			el.IsRenaming = false;
		}
	}

	private void OnRenameCommit(object? sender, RoutedEventArgs e) {
		// LostFocus also fires when IsRenaming is set to false, so skip that case
		if (sender is TextBox { DataContext: HierarchyElement el } && el.IsRenaming)
			CommitRename(el);
	}

	private static async void CommitRename(HierarchyElement el) {
		el.IsRenaming = false;
		var name = el.DraftName?.Trim();
		if (string.IsNullOrEmpty(name) || name == el.Name) return;
		if (s_reservedNames.Contains(name)) {
			await App.Modals.ShowWarning("Reserved Name",
				$"'{name}' is a reserved keyword and cannot be used as a node name.");
			return;
		}

		Events.Send(new NodeChangeName { Node = el.Uid, Name = name });
	}

	// the middle of a row reparents; the top/bottom edge reorders
	private (DropMode mode, HierarchyElement? target) ResolveDrop(DragEventArgs e) {
		if (Vm is not { HasCurrentHierarchy: true } vm) return (DropMode.None, null);
		var position = e.GetPosition(Rows);
		var index = Connector.GetRowAt(position.Y);
		var target = position.X >= 0 && position.X < Rows.Bounds.Width && index >= 0 && index < vm.Rows.Count
			? vm.Rows[index] : null;
		if (target is null) return (DropMode.None, null);
		if (e.DataTransfer.TryGetValue(AssetDragData.Format) is { } asset) {
			return string.Equals(asset.Type, "node", StringComparison.OrdinalIgnoreCase) && target.CanAddChildren
				? (DropMode.Into, target) : (DropMode.None, null);
		}
		var dragged = e.DataTransfer.TryGetValue(NodeDragData.Format);
		if (dragged is not null) dragged = vm.Find(dragged.Uid);
		if (dragged is null || dragged.IsRoot || dragged.IsInsidePrefab || HierarchyViewModel.IsSelfOrDescendant(dragged, target))
			return (DropMode.None, null);

		if (target.IsRoot) return target.CanAddChildren ? (DropMode.Into, target) : (DropMode.None, null);

		var local = e.GetPosition(Rows).Y - Connector.GetRowTop(target.Index);
		if (local < HierarchyConnectorLayer.RowHeight * 0.25 && target.Parent?.CanAddChildren == true) return (DropMode.Before, target);
		if (local > HierarchyConnectorLayer.RowHeight * 0.75 && target.Parent?.CanAddChildren == true) return (DropMode.After, target);
		return target.CanAddChildren ? (DropMode.Into, target) : (DropMode.None, null);
	}

	private void OnDragOver(object? sender, DragEventArgs e) {
		var (mode, target) = ResolveDrop(e);
		e.DragEffects = mode == DropMode.None ? DragDropEffects.None
			: e.DataTransfer.TryGetValue(AssetDragData.Format) is not null ? DragDropEffects.Copy : DragDropEffects.Move;
		e.Handled = true;

		// Into highlight the row
		// Before/After show the insertion line instead
		if (mode is DropMode.Before or DropMode.After && target is not null) {
			SetDropTarget(null);
			var (index, depth) = InsertLine(mode, target);
			Connector.SetDropLine(index, depth);
		} else {
			SetDropTarget(mode == DropMode.Into ? target : null);
			Connector.SetDropLine(-1, 0);
		}
	}

	private void OnDrop(object? sender, DragEventArgs e) {
		var (mode, target) = ResolveDrop(e);
		var dragged = e.DataTransfer.TryGetValue(NodeDragData.Format);
		SetDropTarget(null);
		Connector.SetDropLine(-1, 0);
		e.Handled = true;

		if (target is not null && mode == DropMode.Into && e.DataTransfer.TryGetValue(AssetDragData.Format) is { } asset) {
			if (!target.IsExpanded) Vm?.ToggleExpand(target);
			Events.Send(new WorkspaceSpawn { Parent = target.Uid, Uid = asset.Uid, IsUri = false });
			return;
		}
		if (dragged is null || target is null || mode == DropMode.None || Vm is not { } vm) return;

		if (mode == DropMode.Into) vm.DropInto(dragged, target);
		else vm.DropBeside(dragged, target, mode == DropMode.After);
	}

	// where to draw the insertion line for a sibling drop
	private (int index, int depth) InsertLine(DropMode mode, HierarchyElement target) {
		if (Vm is not { } vm) return (target.Index, target.Depth);
		if (mode == DropMode.Before) return (target.Index, target.Depth);

		var last = target.Index;
		for (var i = target.Index + 1; i < vm.Rows.Count && vm.Rows[i].Depth > target.Depth; i++)
			last = i;
		return (last + 1, target.Depth);
	}

	private void SetDropTarget(HierarchyElement? row) {
		if (ReferenceEquals(m_dropTarget, row)) return;
		if (m_dropTarget is not null) m_dropTarget.IsDropTarget = false;
		m_dropTarget = row;
		if (m_dropTarget is not null) m_dropTarget.IsDropTarget = true;
	}

	private void ClearDropFeedback() {
		SetDropTarget(null);
		Connector.SetDropLine(-1, 0);
	}

	private enum DropMode { None, Into, Before, After }
}
