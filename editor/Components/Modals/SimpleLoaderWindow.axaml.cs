using Avalonia.Controls;
using Avalonia.Threading;

namespace editor.Components.Modals;

public partial class SimpleLoaderWindow : Window {
	public SimpleLoaderWindow() {
		InitializeComponent();
	}

	public SimpleLoaderWindow(LoaderViewModel vm) {
		InitializeComponent();
		DataContext = vm;

		vm.OnClose = () => Dispatcher.UIThread.Post(Close);

		vm.ConsoleLines.CollectionChanged += (_, _) =>
			ConsoleScroll.ScrollToEnd();

		Opened += async (_, _) => await vm.StartAsync();
	}
}
