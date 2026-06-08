//
// ViewportViewModel.cs by Xein
// 2 Jun 2026
//

using Dock.Model.Mvvm.Controls;

namespace editor.Workspace;

public class ViewportViewModel : Document {
	public ViewportViewModel(ToastEngine? engine = null) {
		Engine = engine;
		Title = "Unnamed Node";
		CanDrag = false;
	}

	public ToastEngine? Engine { get; }
}
