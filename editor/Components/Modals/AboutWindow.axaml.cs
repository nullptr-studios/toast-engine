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

	private readonly string[] m_splashFiles;
	private int m_splashIndex = -1;
	private bool m_closing;

	public AboutWindow() {
		InitializeComponent();
		AboutText.Text = AboutContent;
		m_splashFiles = FindSplashImages();
		SetRandomSplashImage();

		// Clicking the cat rolls another one; clicks inside the page never close it
		SplashPanel.PointerPressed += (_, e) => {
			if (e.GetCurrentPoint(SplashPanel).Properties.IsLeftButtonPressed)
				SetRandomSplashImage();
		};

		// Light dismiss: the page goes away once focus leaves it, i.e. on a click anywhere else. Closing deactivates the
		// window too, so ignore the deactivation that Close() itself causes
		Closing += (_, _) => m_closing = true;
		Deactivated += (_, _) => {
			if (!m_closing)
				Close();
		};
		Closed += (_, _) => (SplashImage.Source as Bitmap)?.Dispose();
	}

	private static string[] FindSplashImages() {
		try {
			var folderPath = Path.Combine(AppContext.BaseDirectory, "Resources/splash_images");
			if (!Directory.Exists(folderPath)) return [];

			return Directory.GetFiles(folderPath)
				.Where(f => f.EndsWith(".jpg", StringComparison.OrdinalIgnoreCase) ||
				            f.EndsWith(".jpeg", StringComparison.OrdinalIgnoreCase))
				.ToArray();
		} catch {
			return [];
		}
	}

	private void SetRandomSplashImage() {
		var count = m_splashFiles.Length;
		if (count == 0 || (count == 1 && m_splashIndex == 0)) return;

		// Never roll the picture already on screen, or a click could look like it did nothing
		var next = Random.Shared.Next(m_splashIndex < 0 ? count : count - 1);
		if (m_splashIndex >= 0 && next >= m_splashIndex) next++;

		try {
			var previous = SplashImage.Source as Bitmap;
			SplashImage.Source = new Bitmap(m_splashFiles[next]);
			previous?.Dispose();
			m_splashIndex = next;
		} catch { }
	}
}
