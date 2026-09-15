using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;
using Avalonia.Controls;

namespace editor.Components.Elements;

public interface IAsyncCanExecuteCommand : ICommand {
	Task RefreshCanExecuteAsync(object? parameter);
}

public static class AsyncCommandMenu {
	public static void Attach(ContextMenu menu) {
		menu.Opening += async (_, _) => await RefreshAsync(menu);
	}

	public static async Task RefreshAsync(ContextMenu menu) {
		var refreshes = new List<Task>();
		foreach (var item in menu.Items.OfType<MenuItem>())
			if (item.Command is IAsyncCanExecuteCommand command)
				refreshes.Add(command.RefreshCanExecuteAsync(item.CommandParameter));
		await Task.WhenAll(refreshes);
	}
}
