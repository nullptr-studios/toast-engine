//
// WorkspaceViewModel.cs by Xein
// 2 Jun 2026
//

using Dock.Model.Mvvm.Controls;
using Dock.Model.Core;

namespace editor.MainWindow;

public class ViewportViewModel : ViewModelBase {
	public string label { get; } = "Viewport";
}

public class InspectorViewModel : ViewModelBase {
	public string label { get; } = "Inspector";
}

public class HierarchyViewModel : ViewModelBase {
	public string label { get; } = "Hierarchy";
}
public class WorkspaceViewModel : Document {
	public ViewportViewModel viewport { get; } = new();
	public InspectorViewModel inspector { get; } = new();
	public HierarchyViewModel hierarchy { get; } = new();

	public WorkspaceViewModel(string title = "Untitled") {
		Id = title;
		Title = title;
		CanFloat = true;
		CanDockAsDocument = true;
		AllowedDockOperations = DockOperationMask.Fill | DockOperationMask.Window;
		AllowedDropOperations = DockOperationMask.Fill;
	}
}
