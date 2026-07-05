//
// Hierarchy.axaml.cs by Xein
// 4 Jun 2026
//

using System;
using System.ComponentModel;
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

public partial class HierarchyView : UserControl {
	private const double DragThreshold = 4;

	private HierarchyElement? m_dropTarget;
	private PointerPressedEventArgs? m_pressArgs;

	private HierarchyElement? m_pressItem;
	private Point m_pressPoint;
	private bool m_pressWasSelected;
	private HierarchyViewModel? m_vm;

	public HierarchyView() {
		InitializeComponent();

		Rows.AddHandler(DragDrop.DragOverEvent, OnDragOver);
		Rows.AddHandler(DragDrop.DropEvent, OnDrop);
	}

	private HierarchyViewModel? Vm => DataContext as HierarchyViewModel;

	protected override void OnDataContextChanged(EventArgs e) {
		base.OnDataContextChanged(e);
		if (m_vm is not null) {
			m_vm.HierarchyChanged -= RefreshConnector;
			m_vm.PropertyChanged -= OnVmPropertyChanged;
			m_vm.RenameStarted -= OnRenameStarted;
		}

		m_vm = DataContext as HierarchyViewModel;
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
	}

	private void OnEmptySpaceMenuOpening(object? sender, CancelEventArgs e) {
		if (Vm is { } vm) vm.SelectedNode = null;
	}

	private void OnItemPointerPressed(object? sender, PointerPressedEventArgs e) {
		if (sender is not Control { DataContext: HierarchyElement el } || Vm is not { } vm) return;
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
		if (m_pressItem is { } item && m_pressWasSelected) Vm?.ToggleExpand(item);
		ClearPress();
	}

	private void ClearPress() {
		m_pressItem = null;
		m_pressArgs = null;
		m_pressWasSelected = false;
	}

	private void OnRenameStarted(HierarchyElement element) {
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

	private static void CommitRename(HierarchyElement el) {
		el.IsRenaming = false;
		var name = el.DraftName?.Trim();
		if (string.IsNullOrEmpty(name) || name == el.Name) return;
		Events.Send(new NodeChangeName { Node = el.Uid, Name = name });
		WorkspaceState.MarkModified();
	}

	// the middle of a row reparents; the top/bottom edge reorders
	private (DropMode mode, HierarchyElement? target) ResolveDrop(DragEventArgs e) {
		var dragged = e.DataTransfer.TryGetValue(NodeDragData.Format);
		var target = RowFrom(e.Source);
		if (dragged is null || target is null || HierarchyViewModel.IsSelfOrDescendant(dragged, target))
			return (DropMode.None, null);

		if (target.IsRoot) return (DropMode.Into, target); // root has no siblings

		var local = e.GetPosition(Rows).Y - target.Index * HierarchyConnectorLayer.RowHeight;
		if (local < HierarchyConnectorLayer.RowHeight * 0.25) return (DropMode.Before, target);
		if (local > HierarchyConnectorLayer.RowHeight * 0.75) return (DropMode.After, target);
		return (DropMode.Into, target);
	}

	private void OnDragOver(object? sender, DragEventArgs e) {
		var (mode, target) = ResolveDrop(e);
		e.DragEffects = mode == DropMode.None ? DragDropEffects.None : DragDropEffects.Move;
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

	private static HierarchyElement? RowFrom(object? source) {
		for (var c = source as Control; c is not null; c = c.Parent as Control)
			if (c.DataContext is HierarchyElement row)
				return row;
		return null;
	}

	private enum DropMode { None, Into, Before, After }
}
