using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using editor.Assets.Importers;
using editor.Assets.Types;
using editor.Components.Modals;
using editor.Engine;
using Lucide.Avalonia;

namespace editor.Assets;

public static class AssetDatabase {
	// extension -> engine asset type, derived from the registry
	private static readonly Dictionary<string, string> s_assetTypes =
		AssetTypeRegistry.All.ToDictionary(a => a.Extension, a => a.Type, StringComparer.OrdinalIgnoreCase);

	public static event Action? ReloadedDatabase;

	public static string ComputeHash(string realPath) {
		using var stream = File.OpenRead(realPath);
		var hash = SHA256.HashData(stream);
		return "sha256:" + Convert.ToHexString(hash).ToLowerInvariant();
	}

	public static bool IsUpToDate(string sourceVirtualPath, string hash) {
		AssetStore.Sync();
		return AssetStore.ArtworkByUri(sourceVirtualPath)?.Hash == hash;
	}

	public static void UpdateArtworkDatabase(
		string sourceVirtualPath, string hash, IEnumerable<ArtworkOutput> artworkOutputs) {
		// the importer just wrote the .meta files
		var id = AssetStore.UpsertArtwork(sourceVirtualPath);
		AssetStore.SetArtworkHash(sourceVirtualPath, hash);
		foreach (var output in artworkOutputs)
			AssetStore.SetOutputInfo(output.Uid, output.Name, output.Hash, id);
	}

	public static void SyncArtworkHashes(Action<string>? log = null) {
		AssetStore.Sync();
		var count = 0;
		foreach (var artwork in AssetStore.AllArtwork().Where(a => a.Hash is null)) {
			var real = ProjectContext.Resolve(artwork.Uri);
			if (!File.Exists(real)) continue; // external imports name a source that is not on the project
			AssetStore.SetArtworkHash(artwork.Uri, ComputeHash(real));
			count++;
		}

		if (count > 0) log?.Invoke($"Hashed {count} artwork file(s)");
	}

	public static IReadOnlyList<ArtworkOutput> GetArtworkOutputs(string sourceVirtualPath) {
		AssetStore.Sync();
		return AssetStore.OutputsOf(sourceVirtualPath)
			.Select(a => new ArtworkOutput(a.Uid, a.OutputName ?? Path.GetFileName(a.Path), a.OutputHash, a.Virtual))
			.ToList();
	}

	// syncs the cache from the .meta files
	// if theres a bug it's most likely here
	public static void RebuildAssetDatabase(bool notifyListeners = true) {
		AssetStore.Sync();

		// Refresh UI bind stubs so Lua completion tracks .rml edits
		UIBindStubGenerator.Generate();

		if (ToastEngine.IsEngineReady)
			ToastEngine.ReloadManifest();

		DataSchemaStubGenerator.Generate();

		if (notifyListeners) ReloadedDatabase?.Invoke();
	}

	public static void Reset() { }

	public static bool TryResolve(string uid, out string virtualPath, out string type) {
		if (AssetStore.ByUid(uid) is { } record) {
			virtualPath = record.Virtual;
			type = record.Type;
			return true;
		}

		virtualPath = "";
		type = "";
		return false;
	}

	public static async Task ValidateProject(Action<string>? log = null) {
		log ??= _ => { };
		GenerateMissingMetas(log);
		RebuildAssetDatabase();
		await RelocateMissingAssets(log);
		await RelocateMissingArtwork(log);
		SyncArtworkHashes(log);
		await CheckArtworkChanges(log);
		RebuildAssetDatabase();
	}

	public static void GenerateMissingMetas(Action<string>? log = null) {
		// Include all content databases and core
		var roots = ProjectContext.DatabaseRoots.Append(ProjectContext.CorePath);
		foreach (var root in roots) {
			if (!Directory.Exists(root)) continue;
			foreach (var file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)) {
				var ext = AssetTypeRegistry.GetExtension(Path.GetFileName(file));
				if (!s_assetTypes.TryGetValue(ext, out var type)) continue;
				if (File.Exists(file + ".meta")) continue;

				MetaFile.Write(file, new MetaHeader { Uid = UidGenerator.Generate(), Type = type });
				log?.Invoke($"Generated .meta for {ProjectContext.ToVirtual(file) ?? file}");
			}
		}
	}

	public static void GenerateMissingThumbnails(Action<string>? log = null) {
		var roots = ProjectContext.DatabaseRoots.Append(ProjectContext.CorePath);
		foreach (var root in roots) {
			if (!Directory.Exists(root)) continue;
			foreach (var file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)) {
				if (Path.GetExtension(file).Equals(".meta", StringComparison.OrdinalIgnoreCase)) continue;

				var ext = AssetTypeRegistry.GetExtension(Path.GetFileName(file));
				if (!s_assetTypes.TryGetValue(ext, out _)) continue;
				var definition = AssetTypeRegistry.ByExtension(ext);
				if (definition is not { HasThumbnail: true }) continue;

				var header = MetaFile.ReadHeader(file);
				if (header is null) continue;

				var thumbPath = ProjectContext.Resolve($"cache://thumbnails/{header.Uid}.png");
				if (File.Exists(thumbPath)) continue;

				try {
					definition.GenerateThumbnail(file, header.Uid);
					log?.Invoke($"Generated thumbnail for {ProjectContext.ToVirtual(file) ?? file}");
				} catch (Exception e) {
					log?.Invoke($"warning: could not generate a thumbnail for {ProjectContext.ToVirtual(file) ?? file}: {e.Message}");
				}
			}
		}
	}

	public static async Task RelocateMissingAssets(Action<string> log) {
		var changed = false;
		var discardAll = false;

		var current = ScanCurrentAssets();

		foreach (var row in AssetStore.ListAll()) {
			var uid = row.Uid;
			var virtualPath = row.Virtual;
			if (!AssetStore.IsTrackedScheme(row.Db)) continue;

			var realPath = ProjectContext.Resolve(virtualPath);
			if (File.Exists(realPath)) continue;

			if (current.TryGetValue(uid, out var moved) && moved != virtualPath &&
			    File.Exists(ProjectContext.Resolve(moved))) {
				log($"Moved file: {virtualPath} -> {moved}");
				changed = true; // rebuild rewrites the database
				continue;
			}

			log($"Missing file: {virtualPath}");

			if (discardAll) {
				TryDelete(realPath + ".meta");
				changed = true;
				log($"Discarded {virtualPath}");
				continue;
			}

			var result = await ShowRelocateModal(
				"Missing file",
				$"File {virtualPath} could not be found, do you wish to relocate?",
				"assets://",
				Path.GetExtension(realPath));

			switch (result.Decision) {
				case RelocateDecision.Relocate:
					if (RelocateMeta(realPath, result.PickedPath!, uid, row.Type, log))
						changed = true;
					break;
				case RelocateDecision.Discard:
					if (result.AppliedToAll) discardAll = true;
					TryDelete(realPath + ".meta");
					changed = true;
					log($"Discarded {virtualPath}");
					break;
				case RelocateDecision.Skip:
					log($"Skipped {virtualPath}");
					break;
			}
		}

		if (changed) RebuildAssetDatabase();
	}

	public static async Task RelocateMissingArtwork(Action<string> log) {
		var changed = false;
		var discardAll = false;

		// artwork without a hash counts as unchanged
		foreach (var artwork in AssetStore.AllArtwork().Where(a => a.Hash is not null)) {
			var sourceVirtual = artwork.Uri;
			var realPath = ProjectContext.Resolve(sourceVirtual);
			if (File.Exists(realPath)) continue;

			log($"Missing artwork: {sourceVirtual}");

			// See RelocateMissingAssets()
			if (discardAll) {
				AssetStore.DeleteArtwork(sourceVirtual);
				changed = true;
				log($"Removed link {sourceVirtual}");
				continue;
			}

			var result = await ShowRelocateModal(
				"Missing artwork",
				$"Artwork file {sourceVirtual} could not be found, do you wish to relocate?",
				"artwork://",
				Path.GetExtension(realPath));

			switch (result.Decision) {
				case RelocateDecision.Relocate:
					var newVirtual = ProjectContext.ToVirtual(result.PickedPath!);
					if (newVirtual is null) {
						log($"error: {result.PickedPath} is outside the project, cannot relocate");
						break;
					}

					// repoint every asset generated from this artwork at the new source, then move the artwork
					// onto the new uri
					RepointArtworkSource(sourceVirtual, newVirtual, log);
					AssetStore.RenameArtwork(sourceVirtual, newVirtual);
					changed = true;
					log($"Relocated artwork {sourceVirtual} -> {newVirtual}");
					break;
				case RelocateDecision.Discard:
					if (result.AppliedToAll) discardAll = true;
					AssetStore.DeleteArtwork(sourceVirtual);
					changed = true;
					log($"Removed link {sourceVirtual}");
					break;
				case RelocateDecision.Skip:
					log($"Skipped {sourceVirtual}");
					break;
			}
		}

		if (changed) RebuildAssetDatabase();
	}

	public static async Task CheckArtworkChanges(Action<string> log) {
		var reimported = false;

		foreach (var artwork in AssetStore.AllArtwork().Where(a => a.Hash is not null)) {
			var sourceVirtual = artwork.Uri;
			var realPath = ProjectContext.Resolve(sourceVirtual);
			if (!File.Exists(realPath)) continue; // missing files are handled by RelocateMissingArtwork

			var hash = ComputeHash(realPath);
			if (artwork.Hash == hash) continue;

			log($"Artwork changed: {sourceVirtual}");
			var reimport = await ShowChoice(new ModalConfig(
				"Artwork changed",
				$"Artwork file {sourceVirtual} has changed, do you wish to reimport the assets generated from it?",
				ModalButtons.OkCancel,
				LucideIconKind.RefreshCw,
				new SolidColorBrush(Color.Parse("#4a9eff")),
				"Reimport",
				CancelLabel: "Skip",
				OkIcon: LucideIconKind.FileInput));

			if (reimport == true) {
				await Reimport(sourceVirtual, log);
				reimported = true;
			} else {
				log($"Skipped {sourceVirtual}");
			}
		}

		if (reimported) RebuildAssetDatabase();
	}

	public static async Task Reimport(
		string sourceVirtualPath, Action<string> log,
		Action<double>? progress = null) {
		var previous = GetArtworkOutputs(sourceVirtualPath);
		if (previous.Count == 0) {
			log($"No import record for {sourceVirtualPath}");
			return;
		}

		var realSource = ProjectContext.Resolve(sourceVirtualPath);
		if (!File.Exists(realSource)) {
			log($"Source missing: {sourceVirtualPath}");
			return;
		}

		var uids = previous.Select(o => o.Uid).ToList();

		// find where the existing outputs live + the settings they were imported with
		var (destDir, textureSection, psdSection, gltfSection, voxSection) = LocateOutputs(uids);

		var textureSettings = SettingsFromMeta(textureSection);
		var ext = Path.GetExtension(realSource).ToLowerInvariant();
		// "Master.strings.bank" only has ".bank" as its extension, so the name has to tell the two FMOD importers apart
		var isStringsBank = Path.GetFileName(realSource).EndsWith(".strings.bank", StringComparison.OrdinalIgnoreCase);
		IAssetImporter importer = ext switch {
			".psd" => new PsdImporter(textureSettings, SettingsFromMeta(psdSection)),
			".glb" or ".gltf" => new GltfImporter(SettingsFromMeta(gltfSection), textureSettings),
			".vox" => new VoxImporter(SettingsFromMeta(voxSection)),
			".ttf" => new FontImporter(),
			".tga" => new UIImageImporter(),
			".bank" when isStringsBank => new AudioStringImporter(new AudioStringImporter.Settings()),
			".bank" => new AudioBankImporter(),
			_ => new TextureImporter(textureSettings)
		};

		log($"Reimporting {sourceVirtualPath}...");
		var outputs = await ArtworkImport.RunAsync(importer, realSource, sourceVirtualPath,
			destDir ?? ProjectContext.AssetsPath, previous, log, progress);
		UpdateArtworkDatabase(sourceVirtualPath, ComputeHash(realSource), outputs);
		log("Reimport done.");
	}

	// repoints an asset's UID at a new file by moving its .meta sidecar
	private static bool RelocateMeta(
		string oldRealPath, string newRealPath, string uid, string type,
		Action<string> log) {
		var newVirtual = ProjectContext.ToVirtual(newRealPath);
		if (newVirtual is null) {
			log($"error: {newRealPath} is outside the project, cannot relocate");
			return false;
		}

		var oldMeta = oldRealPath + ".meta";
		var newMeta = newRealPath + ".meta";
		Directory.CreateDirectory(Path.GetDirectoryName(newMeta)!);

		if (File.Exists(oldMeta))
			File.Move(oldMeta, newMeta, true); // keeps the UID + import settings + source
		else
			MetaFile.Write(newRealPath, new MetaHeader { Uid = uid, Type = type }); // keep the database's UID

		log($"Relocated {ProjectContext.ToVirtual(oldRealPath) ?? oldRealPath} -> {newVirtual}");
		return true;
	}

	// rewrites the source of every .meta under assets:// that was generated from oldSource
	private static void RepointArtworkSource(string oldSource, string newSource, Action<string> log) {
		if (!Directory.Exists(ProjectContext.AssetsPath)) return;
		foreach (var metaPath in MetaFile.FindAll(ProjectContext.AssetsPath)) {
			if (MetaFile.ReadHeader(metaPath)?.Source != oldSource) continue;
			if (MetaFile.UpdateSource(metaPath, newSource))
				log($"Repointed {ProjectContext.ToVirtual(metaPath[..^5]) ?? metaPath}");
		}
	}

	private static Dictionary<string, string> ScanCurrentAssets() {
		var current = new Dictionary<string, string>();
		var roots = ProjectContext.DatabaseRoots.Append(ProjectContext.CorePath);
		foreach (var root in roots) {
			if (!Directory.Exists(root)) continue;
			foreach (var metaPath in MetaFile.FindAll(root)) {
				var header = MetaFile.ReadHeader(metaPath);
				if (header is null) continue;
				var virtualPath = ProjectContext.ToVirtual(metaPath[..^5]); // strip ".meta"
				if (virtualPath is not null) current[header.Uid] = virtualPath;
			}
		}

		return current;
	}

	// finds the directory + import settings for a set of output UIDs by scanning their .meta sidecars
	private static (string? destDir, TextureMetaSection? texture, PsdMetaSection? psd, GltfMetaSection? gltf,
		VoxMetaSection? vox)
		LocateOutputs(IReadOnlyCollection<string> uids) {
		if (uids.Count == 0 || !Directory.Exists(ProjectContext.AssetsPath))
			return (null, null, null, null, null);

		var wanted = new HashSet<string>(uids);
		foreach (var metaPath in MetaFile.FindAll(ProjectContext.AssetsPath)) {
			var header = MetaFile.ReadHeader(metaPath);
			if (header is null || !wanted.Contains(header.Uid)) continue;

			var assetReal = metaPath[..^5];
			return (Path.GetDirectoryName(assetReal),
				MetaFile.ReadTextureSection(metaPath),
				MetaFile.ReadPsdSection(metaPath),
				MetaFile.ReadGltfSection(metaPath),
				MetaFile.ReadVoxSection(metaPath));
		}

		return (null, null, null, null, null);
	}

	private static TextureImporter.Settings SettingsFromMeta(TextureMetaSection? s) {
		var settings = new TextureImporter.Settings();
		if (s is null) return settings;

		settings.GenerateMipmaps = s.GenerateMipmaps;
		settings.MaxResolution = s.MaxResolution;
		settings.Anisotropy = s.Anisotropy;
		if (Enum.TryParse<TextureCompression>(s.Compression, true, out var c)) settings.Compression = c;
		if (Enum.TryParse<SuperCompression>(s.SuperCompression, true, out var sc)) settings.SuperCompression = sc;
		if (Enum.TryParse<AddressMode>(s.AddressU, true, out var au)) settings.AddressU = au;
		if (Enum.TryParse<AddressMode>(s.AddressV, true, out var av)) settings.AddressV = av;
		if (Enum.TryParse<FilterMode>(s.Filter, true, out var f)) settings.Filter = f;
		if (Enum.TryParse<TextureColorSpace>(s.ColorSpace, true, out var cs)) settings.ColorSpace = cs;
		return settings;
	}

	private static PsdImporter.Settings SettingsFromMeta(PsdMetaSection? s) {
		var settings = new PsdImporter.Settings();
		if (s is null) return settings;

		settings.CreateFolder = s.CreateFolder;
		if (Enum.TryParse<PsdImportMode>(s.ImportMode, true, out var m)) settings.ImportMode = m;
		return settings;
	}

	private static VoxImporter.Settings SettingsFromMeta(VoxMetaSection? s) {
		var settings = new VoxImporter.Settings();
		if (s is null) return settings;

		settings.CreateSubfolder = s.CreateFolder;
		settings.ImportPalette = s.ImportPalette;
		settings.GeneratePrefab = s.GeneratePrefab;
		return settings;
	}

	private static GltfImporter.Settings SettingsFromMeta(GltfMetaSection? s) {
		var settings = new GltfImporter.Settings();
		if (s is null) return settings;

		settings.CreateSubfolder = s.CreateFolder;
		settings.ImportMaterials = s.ImportMaterials;
		settings.ImportTextures = s.ImportTextures;
		settings.ImportCameras = s.ImportCameras;
		settings.ImportLights = s.ImportLights;
		settings.ImportAnimations = s.ImportAnimations;
		settings.GeneratePrefab = s.GeneratePrefab;
		return settings;
	}

	public static void TryDelete(string path) {
		try {
			if (!File.Exists(path)) return;
			// git-lfs locks are read only so remove that
			File.SetAttributes(path, File.GetAttributes(path) & ~FileAttributes.ReadOnly);
			File.Delete(path);
		} catch {
			/* ignore */
		}
	}

	public static void DeleteDirectory(string path) {
		if (!Directory.Exists(path)) return;
		foreach (var file in Directory.EnumerateFiles(path, "*", SearchOption.AllDirectories))
			File.SetAttributes(file, File.GetAttributes(file) & ~FileAttributes.ReadOnly);
		Directory.Delete(path, true);
	}

	public static void RemoveArtworkOutputs(string uid) {
		RemoveArtworkOutputs([uid]);
	}

	public static void RemoveArtworkOutputs(IEnumerable<string> uids) {
		AssetStore.UnlinkArtwork(uids);
	}

	private static async Task<RelocateResult> ShowRelocateModal(
		string title, string message, string startVirtualPath, string extension) {
		while (true) {
			var (choice, appliedToAll) = await ShowChoiceEx(new ModalConfig(
				title,
				message,
				ModalButtons.OkNoCancel,
				LucideIconKind.FileExclamationPoint,
				Application.Current!.TryGetResource("Yellow", null, out var r) ? r as SolidColorBrush : null,
				"Relocate",
				"Discard",
				"Skip",
				LucideIconKind.Folders,
				LucideIconKind.Shredder,
				NoAllLabel: "Discard All",
				NoAllIcon: LucideIconKind.Trash2));

			switch (choice) {
				case true:
					var picked = await PickFile(title, extension, startVirtualPath);
					if (picked is null) continue; // empty pick -> reopen the popup
					return new RelocateResult(RelocateDecision.Relocate, picked, false);
				case false:
					return new RelocateResult(RelocateDecision.Discard, null, appliedToAll);
				default:
					return new RelocateResult(RelocateDecision.Skip, null, false);
			}
		}
	}

	private static async Task<bool?> ShowChoice(ModalConfig cfg) {
		return (await ShowChoiceEx(cfg)).Choice;
	}

	private static async Task<(bool? Choice, bool AppliedToAll)> ShowChoiceEx(ModalConfig cfg) {
		var owner = ActiveWindow();
		if (owner is null) return (null, false);
		var modal = new MessageModal(cfg);
		var choice = await modal.ShowDialog<bool?>(owner);
		return (choice, modal.AppliedToAll);
	}

	private static async Task<string?> PickFile(string title, string extension, string startVirtualPath) {
		var owner = ActiveWindow();
		var top = owner is null ? null : TopLevel.GetTopLevel(owner);
		if (top is null) return null;

		IStorageFolder? start = null;
		try {
			start = await top.StorageProvider.TryGetFolderFromPathAsync(ProjectContext.Resolve(startVirtualPath));
		} catch {
			// ignore: just opens in the default location
		}

		var ext = extension.TrimStart('.');
		var fileType = string.IsNullOrEmpty(ext)
			? new FilePickerFileType("All files") { Patterns = ["*"] }
			: new FilePickerFileType(ext.ToUpperInvariant() + " file") { Patterns = ["*." + ext] };

		var files = await top.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions {
			Title = title,
			AllowMultiple = false,
			SuggestedStartLocation = start,
			FileTypeFilter = [fileType]
		});

		return files.Count > 0 ? files[0].Path.LocalPath : null;
	}

	// prefer the focused window, fall back to anything open (the loader splash has no "active" window yet)
	private static Window? ActiveWindow() {
		if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
			return desktop.Windows.FirstOrDefault(w => w.IsActive)
				?? desktop.Windows.FirstOrDefault();
		return null;
	}

	private enum RelocateDecision { Relocate, Discard, Skip }

	private readonly record struct RelocateResult(RelocateDecision Decision, string? PickedPath, bool AppliedToAll);
}
