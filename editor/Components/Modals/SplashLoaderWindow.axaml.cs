using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Lucide.Avalonia;

namespace editor.Components.Modals;

public partial class SplashLoaderWindow : Window {
	public SplashLoaderWindow() {
		InitializeComponent();
	}

	public SplashLoaderWindow(LoaderViewModel vm) {
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

		Opened += (_, _) => {
			SetRandomSplashImage();
			Dispatcher.UIThread.InvokeAsync(vm.StartAsync, DispatcherPriority.Background);
		};
	}

	private void SetRandomSplashImage() {
		try {
			var folderPath = Path.Combine(AppContext.BaseDirectory, "Resources/splash_images");
			if (!Directory.Exists(folderPath)) return;

			var files = Directory.GetFiles(folderPath)
				.Where(f => f.EndsWith(".jpg", StringComparison.OrdinalIgnoreCase))
				.ToArray();
			if (files.Length == 0) return;

			SplashImage.Source = new Bitmap(files[new Random().Next(files.Length)]);
		} catch (Exception e) {
			Debug.WriteLine($"Error setting splash image: {e.Message}");
		}
	}
}
