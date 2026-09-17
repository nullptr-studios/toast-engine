using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets.Types;
using Lucide.Avalonia;

namespace editor.Assets.Importers;

public partial class GltfImporter : IAssetImporter {
	private readonly Settings m_settings;
	private readonly TextureImporter m_textureImporter;
	private readonly TextureImporter.Settings m_textureSettings;

	public GltfImporter(Settings settings, TextureImporter.Settings textureSettings) {
		m_settings = settings;
		m_textureSettings = textureSettings;
		m_textureImporter = new TextureImporter(textureSettings);
	}

	public IReadOnlyList<string> SupportedExtensions => [".glb", ".gltf"];

	public bool CanHandle(string filePath) {
		var ext = Path.GetExtension(filePath).ToLowerInvariant();
		return ext is ".glb" or ".gltf";
	}

	public string DisplayName => "Mesh";
	public LucideIconKind Icon => LucideIconKind.Box;

	public BaseAsset PrimaryOutputType => AssetTypeRegistry.ByExtension(".tmesh")!;

	public IReadOnlyList<IAssetImporter> GetAllSettingsImporters() {
		return [this, m_textureImporter];
	}

	public IReadOnlyList<ImporterSetting> GetSettings() {
		return [
			new ImporterSetting("Create Subfolder", SettingKind.Bool,
				() => m_settings.CreateSubfolder,
				v => m_settings.CreateSubfolder = (bool)v!),
			new ImporterSetting("Import Materials", SettingKind.Bool,
				() => m_settings.ImportMaterials,
				v => m_settings.ImportMaterials = (bool)v!),
			new ImporterSetting("Import Textures", SettingKind.Bool,
				() => m_settings.ImportTextures,
				v => m_settings.ImportTextures = (bool)v!),
			new ImporterSetting("Import Cameras", SettingKind.Bool,
				() => m_settings.ImportCameras,
				v => m_settings.ImportCameras = (bool)v!),
			new ImporterSetting("Import Lights", SettingKind.Bool,
				() => m_settings.ImportLights,
				v => m_settings.ImportLights = (bool)v!),
			new ImporterSetting("Import Animations", SettingKind.Bool,
				() => m_settings.ImportAnimations,
				v => m_settings.ImportAnimations = (bool)v!),
			new ImporterSetting("Generate Prefab", SettingKind.Bool,
				() => m_settings.GeneratePrefab,
				v => m_settings.GeneratePrefab = (bool)v!)
		];
	}

	public IReadOnlyList<BaseAsset> OutputTypes => [
		AssetTypeRegistry.ByExtension(".tmesh")!,
		AssetTypeRegistry.ByExtension(".tnode")!,
		AssetTypeRegistry.ByExtension(".ktx2")!,
		AssetTypeRegistry.ByExtension(".tmat")!,
		AssetTypeRegistry.ByExtension(".tanim")!
	];

	/// <summary>
	/// Sampler slots holding measurements, not colour. sRGB-decoding these turns an authored 0.5 into 0.21 -
	/// a normal map skewed toward -X/-Y, or simply the wrong roughness
	/// </summary>
	private static readonly string[] LinearMaterialSlots =
		["gNormal", "gMetallicMap", "gRoughnessMap", "gOcclusionMap"];

	/// <summary>
	/// Reads the material intermediates to find which texture names feed a linear slot
	/// <para>
	/// Scanning the .tmat files the native importer already wrote is what classifies a texture by the job it
	/// does rather than by guessing from its filename
	/// </para>
	/// </summary>
	private static HashSet<string> CollectLinearTextureNames(List<FileInfo> materials, Action<string> log) {
		var linear = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

		foreach (var material in materials) {
			string text;
			try {
				text = File.ReadAllText(material.FullName);
			} catch (Exception ex) {
				// Worst case a texture is imported as sRGB, which is the behaviour that existed before this
				// classification did - not a reason to fail the whole import
				log($"Could not read {material.Name} to classify its textures ({ex.Message}); assuming sRGB");
				continue;
			}

			// toml++ writes each slot as a [gSlot] table with a `texture = '<name>'` entry; the value is
			// single-quoted for identifier-safe names and double-quoted otherwise
			foreach (var slot in LinearMaterialSlots) {
				var header = text.IndexOf($"[{slot}]", StringComparison.Ordinal);
				if (header < 0) continue;

				var textureKey = text.IndexOf("texture", header, StringComparison.Ordinal);
				if (textureKey < 0) continue;

				var lineEnd = text.IndexOf('\n', textureKey);
				var line = lineEnd < 0 ? text[textureKey..] : text[textureKey..lineEnd];

				var open = line.IndexOfAny(['\'', '"']);
				if (open < 0) continue;
				var close = line.IndexOf(line[open], open + 1);
				if (close <= open + 1) continue;    // empty value: the slot has no texture bound

				linear.Add(line[(open + 1)..close]);
			}
		}

		return linear;
	}

	public async Task<IReadOnlyList<string>> Import(
		string realSourcePath, ImportContext ctx, Action<string> log,
		Action<double>? progress = null) {
		var name = Path.GetFileNameWithoutExtension(realSourcePath);
		var destDir = ctx.DestDir;

		// Only create the subfolder when we aren't already standing in it
		if (m_settings.CreateSubfolder && !ImportContext.AlreadyNamed(destDir, name))
			destDir = Path.Combine(destDir, name);

		Directory.CreateDirectory(destDir);

		log($"Generating intermediates in cached://{name}...");
		gltf_generate_intermediates(realSourcePath);

		var tempDir = new DirectoryInfo(Path.Combine(Path.GetFullPath(ProjectContext.CachePath), name));
		// GetFiles() throws rather than returning null, so a null check was guarding the wrong failure: a
		// native parse error produced an empty array and the import silently "succeeded" with nothing in it
		var files = tempDir.Exists ? tempDir.GetFiles() : [];
		if (files.Length == 0) {
			throw new Exception(
				$"GLTF import produced no intermediate files in {tempDir.FullName} - check the editor log for the underlying error");
		}

		var byExtension = files.GroupBy(f => f.Extension).ToDictionary(g => g.Key, g => g.ToList());

		var importedUids = new List<string>();

		// Count total items for fractional progress
		var meshes = byExtension.GetValueOrDefault(".tmesh") ?? [];
		var textures = m_settings.ImportTextures
			? (byExtension.GetValueOrDefault(".png") ?? [])
				.Concat(byExtension.GetValueOrDefault(".jpg") ?? [])
				// The gltf importer already sniffs the KTX2 file signature and saves pre-compressed
				// textures with a .ktx2 extension instead of guessing them into a .png/.jpg - picking
				// those up here is what lets the loop below skip a redundant toktx re-encode
				.Concat(byExtension.GetValueOrDefault(".ktx2") ?? [])
				.ToList()
			: [];
		var materials = m_settings.ImportMaterials ? byExtension.GetValueOrDefault(".tmat") ?? [] : [];
		var animations = m_settings.ImportAnimations ? byExtension.GetValueOrDefault(".tanim") ?? [] : [];
		var scenes = m_settings.GeneratePrefab ? byExtension.GetValueOrDefault(".json") ?? [] : [];
		var totalItems = meshes.Count + textures.Count * 10 + materials.Count + animations.Count +
		                 scenes.Count * 2; // *2: patch + create
		var doneItems = 0;

		void ReportProgress() {
			if (totalItems > 0) progress?.Invoke((double)doneItems / totalItems);
		}

		// Meshes
		var meshUids = new Dictionary<string, string>();
		log($"Importing {meshes.Count} meshes");
		foreach (var m in meshes) {
			var meshName = Path.GetFileNameWithoutExtension(m.Name);
			var destPath = Path.Combine(destDir, meshName + ".tmesh");
			var uid = UidGenerator.Generate();
			meshUids[meshName] = uid;

			log($"Mesh {meshName}");
			log("Creating .tmesh file...");
			File.Copy(m.FullName, destPath, true);

			log("Writing .meta sidecar...");
			var header = new MetaHeader
				{ Uid = uid, Type = AssetTypeRegistry.ByExtension(".tmesh")!.Type, Source = ctx.SourceVirtualPath };
			MetaFile.Write(destPath, header, m_settings.ToSection());
			importedUids.Add(uid);
			doneItems++;
			ReportProgress();
		}

		// Which textures hold data rather than colour, taken from the material slots the native importer
		// wrote them into. Everything but albedo and emissive is measurements - surface direction, how rough,
		// how metallic - and decoding those through sRGB corrupts the value (see TextureColorSpace)
		var linearTextures = CollectLinearTextureNames(materials, log);

		// Textures
		var textureUids = new Dictionary<string, string>();
		if (m_settings.ImportTextures) {
			log($"Importing {textures.Count} textures...");
			foreach (var t in textures) {
				var texName = Path.GetFileNameWithoutExtension(t.Name);
				var destPath = Path.Combine(destDir, texName + ".ktx2");
				var uid = UidGenerator.Generate();
				textureUids[texName] = uid;

				var isLinear = linearTextures.Contains(texName);
				var textureSettings = isLinear ? m_textureSettings.WithColorSpace(TextureColorSpace.Linear) : m_textureSettings;

				log($"Texture {texName}{(isLinear ? " (linear)" : "")}");
				if (t.Extension.Equals(".ktx2", StringComparison.OrdinalIgnoreCase)) {
					log("Already KTX2 - copying directly...");
					File.Copy(t.FullName, destPath, true);
				} else {
					log("Converting to KTX2...");
					await KtxWriter.ConvertTexture(t.FullName, destPath, textureSettings, log);
				}

				log("Generating thumbnail...");
				try {
					// ImageMagick can't decode a .ktx2 (a compressed GPU texture container, not a raster
					// format) - decode it natively instead of reading the pre-conversion raster source
					if (t.Extension.Equals(".ktx2", StringComparison.OrdinalIgnoreCase)) {
						await Task.Run(() => ThumbnailService.GenerateFromKtx2(t.FullName, uid));
					} else {
						await Task.Run(() => ThumbnailService.Generate(t.FullName, uid));
					}
				} catch (Exception ex) {
					// A missing thumbnail is cosmetic; losing the whole import over it is not
					log($"Thumbnail generation failed, continuing without one: {ex.Message}");
				}

				log("Writing .meta sidecar...");
				var header = new MetaHeader
					{ Uid = uid, Type = AssetTypeRegistry.ByExtension(".ktx2")!.Type, Source = ctx.SourceVirtualPath };
				MetaFile.Write(destPath, header, textureSettings.ToSection(), m_settings.ToSection());
				importedUids.Add(uid);
				doneItems += 10;
				ReportProgress();
			}
		}

		// Materials
		var materialUids = new Dictionary<string, string>();
		if (m_settings.ImportMaterials) {
			log($"Importing {materials.Count} materials...");
			foreach (var m in materials) {
				var matName = Path.GetFileNameWithoutExtension(m.Name);
				var destPath = Path.Combine(destDir, matName + ".tmat");
				var uid = UidGenerator.Generate();
				materialUids[matName] = uid;

				log($"Material {matName}");
				log("Creating .tmat file...");
				var toml = await File.ReadAllTextAsync(m.FullName);
				// toml++ writes identifier-safe strings single-quoted, not double-quoted. Matching only the
				// double-quoted form never fired, so materials kept referencing raw texture names
				foreach (var (texName, texUid) in textureUids) {
					toml = toml.Replace($"'{texName}'", $"'{texUid}'").Replace($"\"{texName}\"", $"\"{texUid}\"");
				}
				await File.WriteAllTextAsync(destPath, toml);

				log("Writing .meta sidecar...");
				var header = new MetaHeader
					{ Uid = uid, Type = AssetTypeRegistry.ByExtension(".tmat")!.Type, Source = ctx.SourceVirtualPath };
				MetaFile.Write(destPath, header, m_settings.ToSection());
				importedUids.Add(uid);
				doneItems++;
				ReportProgress();
			}
		}

		// Straight copies - a .tanim references no texture or material UID. But scene nodes reference *them*,
		// so the UIDs are still tracked for the scene patch pass below
		var animationUids = new Dictionary<string, string>();
		if (animations.Count > 0) {
			log($"Importing {animations.Count} animation(s)...");
			foreach (var a in animations) {
				var animName = Path.GetFileNameWithoutExtension(a.Name);
				var destPath = Path.Combine(destDir, animName + ".tanim");
				var uid = UidGenerator.Generate();
				animationUids[animName] = uid;

				log($"Animation {animName}");
				File.Copy(a.FullName, destPath, true);

				log("Writing .meta sidecar...");
				var header = new MetaHeader
					{ Uid = uid, Type = AssetTypeRegistry.ByExtension(".tanim")!.Type, Source = ctx.SourceVirtualPath };
				MetaFile.Write(destPath, header, m_settings.ToSection());
				importedUids.Add(uid);
				doneItems++;
				ReportProgress();
			}
		}

		log("Rebuilding asset database...");
		AssetDatabase.RebuildAssetDatabase();

		// Scenes
		log("Updating scene intermediates with UIDs...");
		foreach (var s in scenes) {
			log($"Scene {Path.GetFileNameWithoutExtension(s.Name)}");

			var json = JsonNode.Parse(await File.ReadAllTextAsync(s.FullName))!;

			void PatchNode(JsonNode node) {
				var nodeType = node["type"]?.GetValue<string>();
				if (nodeType == "toast::MeshNode") {
					var p = node["params"]?.AsObject();
					if (p != null) {
						if (p["mesh"] is { } meshNode && meshUids.TryGetValue(meshNode.GetValue<string>(), out var meshUid))
							p["mesh"] = meshUid;
						if (p["material"] is { } matNode &&
						    materialUids.TryGetValue(matNode.GetValue<string>(), out var matUid))
							p["material"] = matUid;
						if (p["skin_animation"] is { } skinAnimNode &&
						    animationUids.TryGetValue(skinAnimNode.GetValue<string>(), out var skinAnimUid))
							p["skin_animation"] = skinAnimUid;
					}
				} else if (nodeType == "toast::AnimationPlayer") {
					var p = node["params"]?.AsObject();
					if (p != null && p["animation"] is { } animNode &&
					    animationUids.TryGetValue(animNode.GetValue<string>(), out var animUid))
						p["animation"] = animUid;
				}

				if (node["children"] is not JsonArray children) return;

				foreach (var child in children)
					if (child != null)
						PatchNode(child);
			}

			PatchNode(json);
			await File.WriteAllTextAsync(s.FullName, json.ToJsonString());
			doneItems++;
			ReportProgress();
		}

		// Scenes
		log("Creating .tnode scene files...");
		foreach (var s in scenes) {
			var sceneName = Path.GetFileNameWithoutExtension(s.Name);
			var destPath = Path.Combine(destDir, sceneName + ".tnode");
			var uid = UidGenerator.Generate();

			log($"Scene {sceneName}");
			log("Creating .tnode file...");
			gltf_create_tnode(s.FullName, destPath);

			log("Writing .meta sidecar...");
			var header = new MetaHeader
				{ Uid = uid, Type = AssetTypeRegistry.ByExtension(".tnode")!.Type, Source = ctx.SourceVirtualPath };
			MetaFile.Write(destPath, header, m_settings.ToSection());
			importedUids.Add(uid);
			doneItems++;
			ReportProgress();
		}

		log("Rebuilding asset database...");
		AssetDatabase.RebuildAssetDatabase();

		log("Removing intermediates...");
		if (Directory.Exists(tempDir.FullName)) Directory.Delete(tempDir.FullName, true);

		progress?.Invoke(1.0);
		return importedUids;
	}

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void gltf_generate_intermediates(string path);

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void gltf_create_tnode(string jsonPath, string outputPath);

	public partial class Settings : ObservableObject {
		[ObservableProperty] private bool m_createSubfolder = true;
		[ObservableProperty] private bool m_generatePrefab = true;
		[ObservableProperty] private bool m_importAnimations = true;
		[ObservableProperty] private bool m_importCameras;
		[ObservableProperty] private bool m_importLights = true;
		[ObservableProperty] private bool m_importMaterials = true;
		[ObservableProperty] private bool m_importTextures = true;

		public GltfMetaSection ToSection() {
			return new GltfMetaSection {
				CreateFolder = CreateSubfolder,
				ImportMaterials = ImportMaterials,
				ImportTextures = ImportTextures,
				ImportCameras = ImportCameras,
				ImportLights = ImportLights,
				ImportAnimations = ImportAnimations,
				GeneratePrefab = GeneratePrefab
			};
		}
	}
}
