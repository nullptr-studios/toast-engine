using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using editor.Components.Modals;

namespace editor.Assets.Importers;

public partial class CompactImportWindowViewModel : ViewModelBase {
	private readonly IReadOnlyList<string> m_filePaths;
	private readonly List<IAssetImporter> m_importers;
	private readonly ImportTreeState m_state;

	[ObservableProperty] private string m_locationPath;
	[ObservableProperty] private bool m_moveToArtwork = true;
	[ObservableProperty] private string m_artworkDestination = "artwork://";

	private Window? m_window;

	public CompactImportWindowViewModel(IReadOnlyList<string> filePaths, string locationPath) {
		m_filePaths = filePaths;
		m_locationPath = locationPath;
		m_state = ImportTreeState.Load();

		m_importers = [
			new TextureImporter(TextureSettings),
			new PsdImporter(TextureSettings, PsdSettings),
			new GltfImporter(GltfSettings, TextureSettings),
			new AudioBankImporter(),
			new AudioStringImporter(),
		];

		RebuildSettingsCards();
	}

	public ObservableCollection<ImporterSettingsCardVM> SettingsCards { get; } = [];

	public TextureImporter.Settings TextureSettings { get; } = new();
	public PsdImporter.Settings PsdSettings { get; } = new();
	public GltfImporter.Settings GltfSettings { get; } = new();

	public string FileListSummary {
		get {
			if (m_filePaths.Count == 1) return Path.GetFileName(m_filePaths[0]);
			return $"{m_filePaths.Count} files";
		}
	}

	public void SetWindow(Window window) => m_window = window;

	[RelayCommand]
	private async Task BrowseLocation() {
		if (m_window is null) return;
		var result = await AssetTreePicker.PickFolder(m_window);
		if (result is not null) LocationPath = result;
	}

	[RelayCommand]
	private async Task BrowseArtworkDestination() {
		if (m_window is null) return;
		var result = await AssetTreePicker.PickArtworkFolder(m_window, "Select Artwork Destination");
		if (result is not null) ArtworkDestination = result;
	}

	[RelayCommand]
	private async Task Import() {
		string? artworkDir = null;
		if (MoveToArtwork) {
			artworkDir = ProjectContext.Resolve(ArtworkDestination);
			Directory.CreateDirectory(artworkDir);
		}

		var tasks = m_filePaths.Select(filePath => LoaderTask.DoWithProgress(
			Path.GetFileName(filePath),
			async (log, progress) => await ImportFile(filePath, artworkDir, log, progress)
		)).ToList();

		var vm = new LoaderViewModel(tasks) {
			OnComplete = async () => {
				AssetDatabase.RebuildAssetDatabase();
				ProjectContext.RaiseAssetsChanged();
				await Task.CompletedTask;
			}
		};

		var loader = new SimpleLoaderWindow(vm);
		await loader.ShowDialog(m_window!);
		m_window!.Close();
	}

	private async Task ImportFile(string filePath, string? artworkDir, Action<string> log, Action<double> progress) {
		string realSourcePath = filePath;

		if (MoveToArtwork && artworkDir is not null) {
			var artworkDest = Path.Combine(artworkDir, Path.GetFileName(filePath));
			File.Copy(filePath, artworkDest, overwrite: true);
			realSourcePath = artworkDest;
		}

		var ext = Path.GetExtension(realSourcePath).ToLowerInvariant();
		var importer = m_importers.FirstOrDefault(i => i.CanHandle(realSourcePath));
		if (importer is null) {
			log($"No importer for extension '{ext}', skipping.");
			return;
		}

		var destDir = ProjectContext.Resolve(LocationPath);
		Directory.CreateDirectory(destDir);

		if (MoveToArtwork) {
			var sourceVirtual = ProjectContext.ToVirtual(realSourcePath)
			                    ?? throw new InvalidOperationException(
				                    $"File is outside project: {realSourcePath}");
			var hash = AssetDatabase.ComputeHash(realSourcePath);
			if (AssetDatabase.IsUpToDate(sourceVirtual, hash)) {
				log("Already up to date, skipping.");
				return;
			}

			var ctx = new ImportContext { DestDir = destDir, SourceVirtualPath = sourceVirtual };
			var uids = await importer.Import(realSourcePath, ctx, log, progress);
			AssetDatabase.UpdateArtworkDatabase(sourceVirtual, hash, uids);
		} else {
			log("Note: source outside project — reimport unavailable.");
			var fakeVirtual = "artwork://" + Path.GetFileName(realSourcePath);
			var ctx = new ImportContext { DestDir = destDir, SourceVirtualPath = fakeVirtual };
			await importer.Import(realSourcePath, ctx, log, progress);
		}

		log("Done.");
	}

	[RelayCommand]
	private void Cancel() => m_window?.Close();

	private void RebuildSettingsCards() {
		SettingsCards.Clear();
		var seenNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

		foreach (var importer in m_importers) {
			// Does this importer handle ANY of the provided file paths?
			if (!m_filePaths.Any(path => importer.CanHandle(path))) continue;

			// Add settings cards for all settings importers supported by this importer
			foreach (var settingsImporter in importer.GetAllSettingsImporters()) {
				if (seenNames.Add(settingsImporter.DisplayName))
					SettingsCards.Add(new ImporterSettingsCardVM(settingsImporter, m_state));
			}
		}
	}
}
