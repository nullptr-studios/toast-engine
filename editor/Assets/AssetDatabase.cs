using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
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

// two databases because they serve different purposes and have different lifetimes:
//   artwork_database.json  -> maps source artwork paths to output UIDs + hash
//                             used to skip re-importing files that havent changed
//   database.json          -> maps UID to virtual path + asset type
//                             full rebuild on every import, used by the engine at runtime
public static class AssetDatabase {
	private static readonly JsonSerializerOptions s_prettyJson = new() { WriteIndented = true };

	private static Dictionary<string, (string Path, string Type)>? s_uidLookup;

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
		var db = LoadArtworkDatabase();
		return db[sourceVirtualPath] is JsonObject entry
			&& entry["last_hash"]?.GetValue<string>() == hash;
	}

	public static void UpdateArtworkDatabase(
		string sourceVirtualPath, string hash, IEnumerable<string> outputUids) {
		var db = LoadArtworkDatabase();

		var outputs = new JsonArray();
		foreach (var uid in outputUids) outputs.Add(uid);

		db[sourceVirtualPath] = new JsonObject {
			["last_hash"] = hash,
			["outputs"] = outputs
		};

		SaveArtworkDatabase(db);
	}

	// full re-scan of all .meta files, rewrites database.json from scratch
	public static void RebuildAssetDatabase() {
		var db = new JsonObject {
			["version"] = 2,
			["generated_at"] = DateTime.UtcNow.ToString("o")
		};

		ScanDirectory(ProjectContext.AssetsPath, db);
		ScanDirectory(ProjectContext.CorePath, db);

		var path = ProjectContext.Resolve("cache://database.json");
		File.WriteAllText(path, db.ToJsonString(s_prettyJson));

		s_uidLookup = null; // the on-disk database changed, drop the cached lookup

		if (ToastEngine.IsEngineReady)
			ToastEngine.ReloadManifest();

		ReloadedDatabase?.Invoke();
	}

	public static bool TryResolve(string uid, out string virtualPath, out string type) {
		s_uidLookup ??= BuildUidLookup();
		if (s_uidLookup.TryGetValue(uid, out var entry)) {
			virtualPath = entry.Path;
			type = entry.Type;
			return true;
		}

		virtualPath = "";
		type = "";
		return false;
	}

	private static Dictionary<string, (string Path, string Type)> BuildUidLookup() {
		var lookup = new Dictionary<string, (string, string)>();
		if (LoadAssetDatabase() is not { } db) return lookup;

		foreach (var (type, collectionNode) in db) {
			if (type is "version" or "generated_at") continue;
			if (collectionNode is not JsonObject collection) continue;
			foreach (var (uid, pathNode) in collection) {
				var path = pathNode?.GetValue<string>();
				if (path is null) continue;
				lookup[uid] = (path, type);
			}
		}

		return lookup;
	}

	public static async Task ValidateProject(Action<string>? log = null) {
		log ??= _ => { };
		GenerateMissingMetas(log);
		RebuildAssetDatabase();
		await RelocateMissingAssets(log);
		await RelocateMissingArtwork(log);
		await CheckArtworkChanges(log);
		RebuildAssetDatabase();
	}

	public static void GenerateMissingMetas(Action<string>? log = null) {
		foreach (var root in new[] { ProjectContext.AssetsPath, ProjectContext.CorePath }) {
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

	public static async Task RelocateMissingAssets(Action<string> log) {
		if (LoadAssetDatabase() is not { } db) return;

		var changed = false;
		foreach (var (type, collectionNode) in db.ToList()) {
			if (type is "version" or "generated_at") continue;
			if (collectionNode is not JsonObject collection) continue;
			foreach (var (uid, pathNode) in collection.ToList()) {
				var virtualPath = pathNode?.GetValue<string>();
				if (virtualPath is null) continue;

				var realPath = ProjectContext.Resolve(virtualPath);
				if (File.Exists(realPath)) continue;

				log($"Missing file: {virtualPath}");
				var result = await ShowRelocateModal(
					"Missing file",
					$"File {virtualPath} could not be found, do you wish to relocate?",
					"assets://",
					Path.GetExtension(realPath));

				switch (result.Decision) {
					case RelocateDecision.Relocate:
						if (RelocateMeta(realPath, result.PickedPath!, uid, type, log))
							changed = true;
						break;
					case RelocateDecision.Discard:
						TryDelete(realPath + ".meta");
						changed = true;
						log($"Discarded {virtualPath}");
						break;
					case RelocateDecision.Skip:
						log($"Skipped {virtualPath}");
						break;
				}
			}
		}

		if (changed) RebuildAssetDatabase();
	}

	public static async Task RelocateMissingArtwork(Action<string> log) {
		var db = LoadArtworkDatabase();
		var changed = false;

		foreach (var (sourceVirtual, node) in db.ToList()) {
			if (sourceVirtual is "version" or "type") continue;

			var realPath = ProjectContext.Resolve(sourceVirtual);
			if (File.Exists(realPath)) continue;

			log($"Missing artwork: {sourceVirtual}");
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
					// database entry (outputs + hash) onto the new key
					RepointArtworkSource(sourceVirtual, newVirtual, log);
					db.Remove(sourceVirtual); // detaches the node so it can be re-keyed
					db[newVirtual] = node;
					changed = true;
					log($"Relocated artwork {sourceVirtual} -> {newVirtual}");
					break;
				case RelocateDecision.Discard:
					db.Remove(sourceVirtual);
					changed = true;
					log($"Removed link {sourceVirtual}");
					break;
				case RelocateDecision.Skip:
					log($"Skipped {sourceVirtual}");
					break;
			}
		}

		if (changed) SaveArtworkDatabase(db);
	}

	public static async Task CheckArtworkChanges(Action<string> log) {
		var db = LoadArtworkDatabase();
		var reimported = false;

		foreach (var (sourceVirtual, node) in db.ToList()) {
			if (sourceVirtual is "version" or "type") continue;
			if (node is not JsonObject entry) continue;

			var realPath = ProjectContext.Resolve(sourceVirtual);
			if (!File.Exists(realPath)) continue; // missing files are handled by RelocateMissingArtwork

			var hash = ComputeHash(realPath);
			if (entry["last_hash"]?.GetValue<string>() == hash) continue;

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
		var db = LoadArtworkDatabase();
		if (db[sourceVirtualPath] is not JsonObject entry) {
			log($"No import record for {sourceVirtualPath}");
			return;
		}

		var realSource = ProjectContext.Resolve(sourceVirtualPath);
		if (!File.Exists(realSource)) {
			log($"Source missing: {sourceVirtualPath}");
			return;
		}

		var uids = (entry["outputs"] as JsonArray)?
			.Select(n => n!.GetValue<string>())
			.ToList() ?? [];

		// find where the existing outputs live + the settings they were imported with
		var (destDir, textureSection, psdSection, gltfSection) = LocateOutputs(uids);

		var textureSettings = SettingsFromMeta(textureSection);
		var ext = Path.GetExtension(realSource).ToLowerInvariant();
		IAssetImporter importer = ext switch {
			".psd" => new PsdImporter(textureSettings, SettingsFromMeta(psdSection)),
			".glb" or ".gltf" => new GltfImporter(SettingsFromMeta(gltfSection), textureSettings),
			_ => new TextureImporter(textureSettings)
		};

		var ctx = new ImportContext {
			DestDir = destDir ?? ProjectContext.AssetsPath,
			SourceVirtualPath = sourceVirtualPath,
			ReuseUids = uids
		};

		log($"Reimporting {sourceVirtualPath}...");
		var newUids = await importer.Import(realSource, ctx, log, progress);
		UpdateArtworkDatabase(sourceVirtualPath, ComputeHash(realSource), newUids);
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

	// collections are created on demand
	private static void ScanDirectory(string directory, JsonObject db) {
		if (!Directory.Exists(directory)) return;
		foreach (var metaPath in MetaFile.FindAll(directory)) {
			var header = MetaFile.ReadHeader(metaPath);
			if (header is null) continue;

			var assetRealPath = metaPath[..^5]; // strip ".meta"
			var virtualPath = ProjectContext.ToVirtual(assetRealPath);
			if (virtualPath is null) continue;

			if (db[header.Type] is not JsonObject collection) {
				collection = new JsonObject();
				db[header.Type] = collection;
			}

			collection[header.Uid] = virtualPath;
		}
	}

	// finds the directory + import settings for a set of output UIDs by scanning their .meta sidecars
	private static (string? destDir, TextureMetaSection? texture, PsdMetaSection? psd, GltfMetaSection? gltf)
		LocateOutputs(IReadOnlyCollection<string> uids) {
		if (uids.Count == 0 || !Directory.Exists(ProjectContext.AssetsPath))
			return (null, null, null, null);

		var wanted = new HashSet<string>(uids);
		foreach (var metaPath in MetaFile.FindAll(ProjectContext.AssetsPath)) {
			var header = MetaFile.ReadHeader(metaPath);
			if (header is null || !wanted.Contains(header.Uid)) continue;

			var assetReal = metaPath[..^5];
			return (Path.GetDirectoryName(assetReal),
				MetaFile.ReadTextureSection(metaPath),
				MetaFile.ReadPsdSection(metaPath),
				MetaFile.ReadGltfSection(metaPath));
		}

		return (null, null, null, null);
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
		return settings;
	}

	private static PsdImporter.Settings SettingsFromMeta(PsdMetaSection? s) {
		var settings = new PsdImporter.Settings();
		if (s is null) return settings;

		settings.CreateFolder = s.CreateFolder;
		if (Enum.TryParse<PsdImportMode>(s.ImportMode, true, out var m)) settings.ImportMode = m;
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
		settings.GeneratePrefab = s.GeneratePrefab;
		return settings;
	}

	private static JsonObject? LoadAssetDatabase() {
		var path = ProjectContext.Resolve("cache://database.json");
		if (!File.Exists(path)) return null;
		try {
			return JsonNode.Parse(File.ReadAllText(path)) as JsonObject;
		} catch {
			return null;
		}
	}

	private static JsonObject LoadArtworkDatabase() {
		var path = ProjectContext.Resolve("cache://artwork_database.json");
		if (!File.Exists(path))
			return new JsonObject {
				["version"] = 1,
				["type"] = "artwork_database"
			};
		try {
			return JsonNode.Parse(File.ReadAllText(path)) as JsonObject
				?? new JsonObject { ["version"] = 1, ["type"] = "artwork_database" };
		} catch {
			return new JsonObject { ["version"] = 1, ["type"] = "artwork_database" };
		}
	}

	private static void SaveArtworkDatabase(JsonObject db) {
		var path = ProjectContext.Resolve("cache://artwork_database.json");
		File.WriteAllText(path, db.ToJsonString(s_prettyJson));
	}

	public static void TryDelete(string path) {
		try {
			if (File.Exists(path)) File.Delete(path);
		} catch {
			/* ignore */
		}
	}

	public static void RemoveArtworkOutputs(string uid) {
		var db = LoadArtworkDatabase();
		var changed = false;
		foreach (var key in db.Select(kv => kv.Key).ToList()) {
			if (key is "version" or "type") continue;
			if (db[key] is not JsonObject entry) continue;
			if (entry["outputs"] is not JsonArray outputs) continue;
			for (var i = outputs.Count - 1; i >= 0; i--) {
				if (outputs[i]?.GetValue<string>() != uid) continue;
				outputs.RemoveAt(i);
				changed = true;
			}

			if (outputs.Count == 0)
				db.Remove(key);
		}

		if (changed) SaveArtworkDatabase(db);
	}

	private static async Task<RelocateResult> ShowRelocateModal(
		string title, string message, string startVirtualPath, string extension) {
		while (true) {
			var choice = await ShowChoice(new ModalConfig(
				title,
				message,
				ModalButtons.OkNoCancel,
				LucideIconKind.FileExclamationPoint,
				Application.Current!.TryGetResource("Yellow", null, out var r) ? r as SolidColorBrush : null,
				"Relocate",
				"Discard",
				"Skip",
				LucideIconKind.Folders,
				LucideIconKind.Shredder));

			switch (choice) {
				case true:
					var picked = await PickFile(title, extension, startVirtualPath);
					if (picked is null) continue; // empty pick -> reopen the popup
					return new RelocateResult(RelocateDecision.Relocate, picked);
				case false:
					return new RelocateResult(RelocateDecision.Discard, null);
				default:
					return new RelocateResult(RelocateDecision.Skip, null);
			}
		}
	}

	private static async Task<bool?> ShowChoice(ModalConfig cfg) {
		var owner = ActiveWindow();
		if (owner is null) return null;
		return await new MessageModal(cfg).ShowDialog<bool?>(owner);
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

	private readonly record struct RelocateResult(RelocateDecision Decision, string? PickedPath);
}
