//
// Hierarchy.axaml.cs by Xein
// 4 Jun 2026
//

using System.ComponentModel;
using Avalonia.Controls;

namespace editor.Workspace;

public partial class HierarchyView : UserControl {
	public HierarchyView() {
		InitializeComponent();
	}

	private void OnEmptySpaceMenuOpening(object? sender, CancelEventArgs e) {
		Tree.SelectedItem = null;
	}
}
