//
// ViewportViewModel.cs by Xein
// 2 Jun 2026
//

using Dock.Model.Core;
using Dock.Model.Mvvm.Controls;

namespace editor.Workspace;

public class InspectorViewModel : Tool {
	public InspectorViewModel() {
		Id = "Inspector";
		Title = "Inspector";
		CanFloat = true;
		CanDockAsDocument = false;   // blocks docking into the document/center (AllowsDocumentDocking)
		AllowedDockOperations = DockOperationMask.Fill | DockOperationMask.Left
		                      | DockOperationMask.Right | DockOperationMask.Window;
		AllowedDropOperations = DockOperationMask.Fill;
	}
}

public class HierarchyViewModel : Tool {
	public HierarchyViewModel() {
		Id = "Hierarchy";
		Title = "Hierarchy";
		CanFloat = true;
		CanDockAsDocument = false;   // blocks docking into the document/center (AllowsDocumentDocking)
		AllowedDockOperations = DockOperationMask.Fill | DockOperationMask.Left
		                      | DockOperationMask.Right | DockOperationMask.Window;
		AllowedDropOperations = DockOperationMask.Fill;
	}
}

public class ViewportViewModel : Document {
	public ToastEngine? Engine { get; }

	public ViewportViewModel(string title = "Untitled", ToastEngine? engine = null) {
		Engine = engine;
		Id = title;
		Title = title;
		CanFloat = false;
		CanDockAsDocument = true;
		// Fill = tab, Left/Right = side-by-side scene split. No Window: can't tear off to a main window.
		AllowedDockOperations = DockOperationMask.Fill | DockOperationMask.Left | DockOperationMask.Right;
		AllowedDropOperations = DockOperationMask.Fill | DockOperationMask.Left | DockOperationMask.Right;
	}
}
