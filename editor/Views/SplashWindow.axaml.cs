using System;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using Avalonia.Media.Imaging;

namespace editor.Views;

public partial class SplashWindow : Window {
	public SplashWindow() {
		InitializeComponent();
	}

	protected override void OnOpened(EventArgs e) {
		base.OnOpened(e);
		setSplashImage();
	}

	private void setSplashImage() {
		try {
			var folder_path = Path.Combine(AppContext.BaseDirectory, "Assets/splash_images");
			if (!Directory.Exists(folder_path)) return;

			var files = Directory.GetFiles(folder_path).Where(f=> f.EndsWith(".jpg", StringComparison.OrdinalIgnoreCase)).ToArray();
			if (files.Length <= 0) return;

			var random = new Random();
			var random_image = files[random.Next(files.Length)];
			SplashImage.Source = new Bitmap(random_image);

		}
		catch (Exception e) {
			System.Diagnostics.Debug.WriteLine($"Error setting splash image: {e.Message}");
			throw;
		}
	}
}