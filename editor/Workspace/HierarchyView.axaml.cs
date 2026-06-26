//
// Hierarchy.axaml.cs by Xein
// 4 Jun 2026
//

using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using editor.Components.Elements;

namespace editor.Workspace;

public partial class HierarchyView : UserControl {
	private const double DragThreshold = 4;

	private HierarchyElement? m_pressItem;
	private Point m_pressPoint;
	private PointerPressedEventArgs? m_pressArgs;

	public HierarchyView() {
		InitializeComponent();
	}

	private void OnEmptySpaceMenuOpening(object? sender, CancelEventArgs e) {
		Tree.SelectedItem = null;
	}

	private void OnItemPointerPressed(object? sender, PointerPressedEventArgs e) {
		if (sender is not Control { DataContext: HierarchyElement el }) return;
		if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		m_pressItem = el;
		m_pressArgs = e;
		m_pressPoint = e.GetPosition(this);
	}

	private async void OnItemPointerMoved(object? sender, PointerEventArgs e) {
		if (m_pressItem is null || m_pressArgs is null || !e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
		var delta = e.GetPosition(this) - m_pressPoint;
		if (delta.X * delta.X + delta.Y * delta.Y < DragThreshold * DragThreshold) return;

		var item = m_pressItem;
		var args = m_pressArgs;
		m_pressItem = null;
		m_pressArgs = null;

		var data = new DataTransfer();
		data.Add(DataTransferItem.Create(NodeDragData.Format, item));
		await DragDrop.DoDragDropAsync(args, data, DragDropEffects.Copy);
	}

	private void OnItemPointerReleased(object? sender, PointerReleasedEventArgs e) {
		m_pressItem = null;
		m_pressArgs = null;
	}
}
