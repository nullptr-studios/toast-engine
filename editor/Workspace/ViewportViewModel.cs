//
// ViewportViewModel.cs by Xein
// 2 Jun 2026
//

using CommunityToolkit.Mvvm.ComponentModel;

namespace editor.Workspace;

public class InspectorViewModel : ObservableObject { }

public class HierarchyViewModel : ObservableObject { }

public class ViewportViewModel : ObservableObject {
	public ViewportViewModel(string title = "Scene", ToastEngine? engine = null) {
		Engine = engine;
		Title = title;
	}

	public string Title { get; } = "Scene";
	public ToastEngine? Engine { get; }
}
