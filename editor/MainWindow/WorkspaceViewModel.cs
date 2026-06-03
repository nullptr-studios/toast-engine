//
// WorkspaceViewModel.cs by Xein
// 2 Jun 2026
//

using Dock.Model.Mvvm.Controls;
using Dock.Model.Core;

namespace editor.MainWindow;

public class ViewportViewModel(ToastEngine? engine = null) : ViewModelBase {
	public string Label { get; } = "Viewport";

	/// @brief Engine used by the viewport control to pull frames and forward input. May be null
	/// (e.g. when a layout is deserialized without an engine), in which case the viewport renders nothing.
	public ToastEngine? Engine { get; } = engine;
}

public class InspectorViewModel : ViewModelBase {
	public string Label { get; } = "Inspector";
}

public class HierarchyViewModel : ViewModelBase {
	public string Label { get; } = "Hierarchy";
}
public class WorkspaceViewModel : Document {
	public ViewportViewModel Viewport { get; }
	public InspectorViewModel Inspector { get; } = new();
	public HierarchyViewModel Hierarchy { get; } = new();

	public WorkspaceViewModel(string title = "Untitled", ToastEngine? engine = null) {
		Viewport = new ViewportViewModel(engine);
		Id = title;
		Title = title;
		CanFloat = true;
		CanDockAsDocument = true;
		AllowedDockOperations = DockOperationMask.Fill | DockOperationMask.Window;
		AllowedDropOperations = DockOperationMask.Fill;
	}
}
