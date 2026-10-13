using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace editor.Workspace;

// viewport rendering and input forwarding live in ViewportControl; toolbar state in WorkspaceViewModel
public partial class WorkspaceView : UserControl, IWorkspaceShortcutTarget {
	public WorkspaceView() {
		InitializeComponent();
		AddHandler(KeyDownEvent, OnShortcut, RoutingStrategies.Tunnel);
	}

	private void OnShortcut(object? sender, KeyEventArgs e) {
		if (DataContext is WorkspaceViewModel vm) ToolShortcuts.Run(e, vm);
	}
}

// gizmo tool, space and snap shortcuts; the main window runs them for the active workspace so they work wherever focus is
public static class ToolShortcuts {
	public static bool Run(KeyEventArgs e, WorkspaceViewModel vm) {
		if (e.Handled || PlayModeShortcuts.Blocked || ViewportControl.AnyEditorFlying) return false;

		var mods = e.KeyModifiers;

		switch (e.Key) {
			case Key.Q when mods == KeyModifiers.None:
				vm.SetToolCommand.Execute("Select");
				break;
			case Key.E when mods == KeyModifiers.None:
				vm.SetToolCommand.Execute("Translate");
				break;
			case Key.R when mods == KeyModifiers.None:
				vm.SetToolCommand.Execute("Rotate");
				break;
			case Key.T when mods == KeyModifiers.None:
				vm.SetToolCommand.Execute("Scale");
				break;
			case Key.Y when mods == KeyModifiers.None:
				vm.SetToolCommand.Execute("Ruler");
				break;
			case Key.F when mods == KeyModifiers.None:
				vm.SetSpaceCommand.Execute(vm.WorldSpace ? "Local" : "World");
				break;
			case Key.G when mods == KeyModifiers.Shift:
				vm.RotateSnapEnabled = !vm.RotateSnapEnabled;
				break;
			case Key.G when mods == KeyModifiers.Alt:
				vm.ScaleSnapEnabled = !vm.ScaleSnapEnabled;
				break;
			case Key.G when mods == KeyModifiers.None:
				vm.TranslateSnapEnabled = !vm.TranslateSnapEnabled;
				break;
			default:
				return false;
		}

		e.Handled = true;
		return true;
	}
}
