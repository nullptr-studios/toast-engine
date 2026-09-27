using System;
using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace editor.Workspace;

// the game owns the keyboard while any workspace plays so no editor shortcut may fire
internal static class PlayModeShortcuts {
	public static bool Blocked => WorkspaceViewModel.AnyPlayActive;

	// KeyBindings fire before any routed handler and only check CanExecute so their commands get wrapped
	// app wide instead of disabled which would also grey out the menus
	public static void Install() {
		KeyBinding.CommandProperty.Changed.AddClassHandler<KeyBinding>((binding, e) => {
			if (e.NewValue is ICommand command and not GuardedCommand)
				binding.SetCurrentValue(KeyBinding.CommandProperty, new GuardedCommand(command));
		});

		// the window access key handler opens the main menu on a lone Alt release and runs Alt+letter
		// class handlers go first so it never sees them and ViewportControl forwards Alt itself
		InputElement.KeyDownEvent.AddClassHandler<Window>(SwallowAlt, RoutingStrategies.Tunnel);
		InputElement.KeyUpEvent.AddClassHandler<Window>(SwallowAlt, RoutingStrategies.Tunnel);
		InputElement.KeyDownEvent.AddClassHandler<Window>(SwallowAccessKey, RoutingStrategies.Bubble);
	}

	public static bool IsAlt(Key key) => key is Key.LeftAlt or Key.RightAlt;

	private static void SwallowAlt(Window window, KeyEventArgs e) {
		if (Blocked && IsAlt(e.Key)) e.Handled = true;
	}

	private static void SwallowAccessKey(Window window, KeyEventArgs e) {
		if (Blocked && e.KeyModifiers.HasFlag(KeyModifiers.Alt)) e.Handled = true;
	}

	private sealed class GuardedCommand(ICommand inner) : ICommand {
		public event EventHandler? CanExecuteChanged {
			add => inner.CanExecuteChanged += value;
			remove => inner.CanExecuteChanged -= value;
		}

		public bool CanExecute(object? parameter) => !Blocked && inner.CanExecute(parameter);

		public void Execute(object? parameter) {
			if (CanExecute(parameter)) inner.Execute(parameter);
		}
	}
}
