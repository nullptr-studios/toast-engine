using System;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Media.Imaging;

namespace editor.Components.Modals;

public partial class AboutWindow : Window {
	public const string AboutContent =
		"""
		Toast Engine v2
		built with <3 by nullptr* studios

		Credits:
		Xein
		Dario
		Dante
		Alexey, Iñaki, Akaansh

		Cats:
		Toast, Ulysses, Kenzo, Milky, Grandma1, Grandma2

		Tools:
		Vulkan, Avalonia, FMOD, glm, SDL3, Tracy Profiler, ktx2,
		nlohmann-json, toml++, tinygltf, lz4, lua, luabridge, rmlUI,
		protobuff, vcpkg, ratatui, cmake,ninja, clang, JetBrains Rider,
		JetBrains CLion, Neovim, Doxygen, Graphviz, .NET sdk, cargo
		""";

	public AboutWindow() {
		InitializeComponent();
		AboutText.Text = AboutContent;
		SetRandomSplashImage();

		PointerPressed += (_, e) => {
			if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
				Close();
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
		} catch { }
	}
}
