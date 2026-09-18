using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets.Types;
using Lucide.Avalonia;

namespace editor.Assets.Importers;

public partial class VoxImporter : IAssetImporter {
	private readonly Settings m_settings;

	public VoxImporter(Settings settings) {
		m_settings = settings;
	}

	public IReadOnlyList<string> SupportedExtensions => [".vox"];

	public bool CanHandle(string filePath) {
		return Path.GetExtension(filePath).Equals(".vox", StringComparison.OrdinalIgnoreCase);
	}

	public string DisplayName => "Voxel Model";
	public LucideIconKind Icon => LucideIconKind.Box;

	public BaseAsset PrimaryOutputType => AssetTypeRegistry.ByExtension(".tvox")!;

	public IReadOnlyList<BaseAsset> OutputTypes => [
		AssetTypeRegistry.ByExtension(".tvox")!,
		AssetTypeRegistry.ByExtension(".tpal")!,
		AssetTypeRegistry.ByExtension(".tnode")!
	];

	public IReadOnlyList<ImporterSetting> GetSettings() {
		return [
			new ImporterSetting("Create Subfolder", SettingKind.Bool,
				() => m_settings.CreateSubfolder,
				v => m_settings.CreateSubfolder = (bool)v!),
			new ImporterSetting("Import Palette", SettingKind.Bool,
				() => m_settings.ImportPalette,
				v => m_settings.ImportPalette = (bool)v!),
			new ImporterSetting("Generate Prefab", SettingKind.Bool,
				() => m_settings.GeneratePrefab,
				v => m_settings.GeneratePrefab = (bool)v!)
		];
	}

	public async Task<IReadOnlyList<string>> Import(
		string realSourcePath, ImportContext ctx, Action<string> log,
		Action<double>? progress = null) {
		var name = Path.GetFileNameWithoutExtension(realSourcePath);
		var destDir = ctx.DestDir;

		if (m_settings.CreateSubfolder && !ImportContext.AlreadyNamed(destDir, name))
			destDir = Path.Combine(destDir, name);

		Directory.CreateDirectory(destDir);

		// UIDs are kept by file not by output position
		var assignedUids = new HashSet<string>();

		string UidOf(string destPath) {
			var existing = File.Exists(destPath) ? MetaFile.ReadHeader(destPath)?.Uid : null;
			var uid = !string.IsNullOrEmpty(existing) && !assignedUids.Contains(existing) ? existing : UidGenerator.Generate();
			assignedUids.Add(uid);
			return uid;
		}

		// The palette UID must exist first since each .tvox stores it
		var paletteDest = Path.Combine(destDir, name + ".tpal");
		var paletteExisted = File.Exists(paletteDest);
		var paletteUid = UidOf(paletteDest);

		var tempDir = Path.Combine(Path.GetFullPath(ProjectContext.CachePath), name);
		if (Directory.Exists(tempDir)) Directory.Delete(tempDir, true);

		log($"Reading {Path.GetFileName(realSourcePath)}...");
		await Task.Run(() => vox_generate_intermediates(realSourcePath, tempDir, paletteUid));

		// An empty directory means the native parse failed
		var temp = new DirectoryInfo(tempDir);
		var models = temp.Exists ? temp.GetFiles("*.tvox").OrderBy(f => f.Name).ToList() : [];
		if (models.Count == 0) {
			throw new Exception(
				$"Voxel import produced no models in {tempDir} - check the editor log for the underlying error");
		}

		var manifest = temp.GetFiles("*.json").FirstOrDefault();
		var importedUids = new List<string>();
		var totalItems = models.Count + 2;
		var doneItems = 0;

		void ReportProgress() {
			progress?.Invoke((double)doneItems / totalItems);
		}

		var modelUids = new Dictionary<string, string>();
		log($"Importing {models.Count} voxel model(s)...");
		foreach (var model in models) {
			var destPath = Path.Combine(destDir, model.Name);
			var uid = UidOf(destPath);
			modelUids[model.Name] = uid;

			log($"Voxel model {Path.GetFileNameWithoutExtension(model.Name)}");
			File.Copy(model.FullName, destPath, true);

			var header = new MetaHeader
				{ Uid = uid, Type = AssetTypeRegistry.ByExtension(".tvox")!.Type, Source = ctx.SourceVirtualPath };
			MetaFile.Write(destPath, header, m_settings.ToSection());
			importedUids.Add(uid);
			doneItems++;
			ReportProgress();
		}

		// Palette on first import only because materials are assigned by hand
		var palette = temp.GetFiles("*.tpal").FirstOrDefault();
		if (m_settings.ImportPalette && palette is not null) {
			if (paletteExisted) {
				log($"Keeping the existing palette {name}.tpal - a reimport would discard its material assignments");
			} else {
				log("Creating .tpal file...");
				File.Copy(palette.FullName, paletteDest, true);
				var header = new MetaHeader {
					Uid = paletteUid, Type = AssetTypeRegistry.ByExtension(".tpal")!.Type, Source = ctx.SourceVirtualPath
				};
				MetaFile.Write(paletteDest, header, m_settings.ToSection());
			}
			importedUids.Add(paletteUid);
		}
		doneItems++;
		ReportProgress();

		if (m_settings.GeneratePrefab && manifest is not null) {
			log("Updating the scene manifest with UIDs...");
			var json = await File.ReadAllTextAsync(manifest.FullName);
			foreach (var (fileName, uid) in modelUids) {
				json = json.Replace($"\"{fileName}\"", $"\"{uid}\"");
			}
			if (palette is not null) {
				json = json.Replace($"\"{palette.Name}\"", $"\"{paletteUid}\"");
			}
			await File.WriteAllTextAsync(manifest.FullName, json);

			var destPath = Path.Combine(destDir, name + ".tnode");
			var prefabUid = UidOf(destPath);
			log("Creating .tnode file...");
			await Task.Run(() => vox_create_tnode(manifest.FullName, destPath));

			var header = new MetaHeader {
				Uid = prefabUid, Type = AssetTypeRegistry.ByExtension(".tnode")!.Type, Source = ctx.SourceVirtualPath
			};
			MetaFile.Write(destPath, header, m_settings.ToSection());
			importedUids.Add(prefabUid);
		}
		doneItems++;
		ReportProgress();

		log("Rebuilding asset database...");
		AssetDatabase.RebuildAssetDatabase();

		log("Removing intermediates...");
		if (Directory.Exists(tempDir)) Directory.Delete(tempDir, true);

		progress?.Invoke(1.0);
		return importedUids;
	}

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void vox_generate_intermediates(string sourcePath, string outDir, string paletteUid);

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void vox_create_tnode(string manifestPath, string outputPath);

	public partial class Settings : ObservableObject {
		[ObservableProperty] private bool m_createSubfolder = true;
		[ObservableProperty] private bool m_generatePrefab = true;
		[ObservableProperty] private bool m_importPalette = true;

		public VoxMetaSection ToSection() {
			return new VoxMetaSection {
				CreateFolder = CreateSubfolder,
				ImportPalette = ImportPalette,
				GeneratePrefab = GeneratePrefab
			};
		}
	}
}
