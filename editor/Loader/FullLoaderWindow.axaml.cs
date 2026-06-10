using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Media.Imaging;
using Avalonia.Threading;

namespace editor.Loader;

public partial class FullLoaderWindow : Window {
	public FullLoaderWindow() {
		InitializeComponent();
	}

	public FullLoaderWindow(LoaderViewModel vm) {
		InitializeComponent();
		DataContext = vm;

		vm.OnClose = () => Dispatcher.UIThread.Post(Close);

		vm.ConsoleLines.CollectionChanged += (_, _) =>
			ConsoleScroll.ScrollToEnd();

		Opened += async (_, _) => {
			SetRandomSplashImage();
			await vm.StartAsync();
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
