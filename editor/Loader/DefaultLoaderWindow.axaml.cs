using Avalonia.Controls;
using Avalonia.Threading;

namespace editor.Loader;

public partial class DefaultLoaderWindow : Window {
	public DefaultLoaderWindow() {
		InitializeComponent();
	}

	public DefaultLoaderWindow(LoaderViewModel vm) {
		InitializeComponent();
		DataContext = vm;

		vm.OnClose = () => Dispatcher.UIThread.Post(Close);

		// Auto-scroll the console to the bottom as new lines arrive
		vm.ConsoleLines.CollectionChanged += (_, _) =>
			ConsoleScroll.ScrollToEnd();

		Opened += async (_, _) => await vm.StartAsync();
	}
}
