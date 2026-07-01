using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Threading;
using Lucide.Avalonia;

namespace editor.Components.Modals;

public partial class SimpleLoaderWindow : Window {
	public SimpleLoaderWindow() {
		InitializeComponent();
	}

	public SimpleLoaderWindow(LoaderViewModel vm) {
		InitializeComponent();
		DataContext = vm;

		vm.OnClose = () => Dispatcher.UIThread.Post(Close);
		vm.OnTaskError = async (title, msg) => await new MessageModal(new ModalConfig(
			title, msg,
			Icon: LucideIconKind.CircleX,
			IconColor: Application.Current!.TryGetResource("Red", null, out var r) ? r as SolidColorBrush : null
		)).ShowDialog(this);

		vm.ConsoleLines.CollectionChanged += (_, _) =>
			ConsoleScroll.ScrollToEnd();

		Opened += (_, _) => Dispatcher.UIThread.InvokeAsync(vm.StartAsync, DispatcherPriority.Background);
	}
}
