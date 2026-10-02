using System.Windows.Input;
using Avalonia.Input;

namespace editor.Workspace;

public static class EditShortcuts {
	public static bool Run(KeyEventArgs e, ICommand? undo, ICommand? redo, HierarchyViewModel? hierarchy) {
		var ctrl = e.KeyModifiers == KeyModifiers.Control;
		var ctrlShift = e.KeyModifiers == (KeyModifiers.Control | KeyModifiers.Shift);
		ICommand? command = null;
		object? parameter = null;

		if (ctrl && e.Key == Key.Z) command = undo;
		else if ((ctrlShift && e.Key == Key.Z) || (ctrl && e.Key == Key.Y)) command = redo;
		else if (ctrl && e.Key == Key.A) command = hierarchy?.AddNodeCommand;
		else if (ctrlShift && e.Key == Key.A) command = hierarchy?.LoadNodeCommand;
		else if (ctrl && e.Key == Key.X) command = hierarchy?.CutCommand;
		else if (ctrl && e.Key == Key.C) command = hierarchy?.CopyCommand;
		else if (ctrl && e.Key == Key.V) command = hierarchy?.PasteCommand;
		else if (ctrlShift && e.Key == Key.V) command = hierarchy?.PasteAsChildCommand;
		else if (ctrl && e.Key == Key.D) command = hierarchy?.DuplicateCommand;
		else if (ctrl && e.Key == Key.Up) command = hierarchy?.MoveUpCommand;
		else if (ctrl && e.Key == Key.Down) command = hierarchy?.MoveDownCommand;
		else if (e.KeyModifiers == KeyModifiers.None && e.Key == Key.F2) command = hierarchy?.RenameCommand;
		else if (e.KeyModifiers == KeyModifiers.None && e.Key == Key.Delete) command = hierarchy?.DeleteCommand;

		if (command is null) return false;
		parameter = hierarchy?.SelectedNode;
		if (!command.CanExecute(parameter)) return false;
		command.Execute(parameter);
		e.Handled = true;
		return true;
	}
}
