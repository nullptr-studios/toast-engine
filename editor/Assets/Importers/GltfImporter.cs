using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using CommunityToolkit.Mvvm.ComponentModel;
using editor.Assets;
using editor.Assets.Types;
using editor.Workspace;

namespace editor.Assets.Importers;

public partial class GltfImporter : IAssetImporter {
	private readonly Settings m_settings;
	private readonly TextureImporter.Settings m_textureSettings;

	public GltfImporter(Settings settings, TextureImporter.Settings textureSettings) {
		m_settings = settings;
		m_textureSettings = textureSettings;
	}

	public IReadOnlyList<string> SupportedExtensions => [".glb"];

	public BaseAsset PrimaryOutputType => AssetTypeRegistry.ByExtension(".tmesh")!;

	public IReadOnlyList<BaseAsset> OutputTypes => [
		AssetTypeRegistry.ByExtension(".tmesh")!,
		AssetTypeRegistry.ByExtension(".tnode")!,
		AssetTypeRegistry.ByExtension(".ktx2")!,
		AssetTypeRegistry.ByExtension(".tmat")!,
	];

	public async Task<IReadOnlyList<string>> Import(string realSourcePath, ImportContext ctx, Action<string> log) {
		var name = Path.GetFileNameWithoutExtension(realSourcePath);
		var destDir = ctx.DestDir;

		if (m_settings.CreateSubfolder) {
			destDir = Path.Combine(ctx.DestDir, name);
		}

		Directory.CreateDirectory(destDir);

		log($"Generating intermediates in cached://{name}...");
		gltf_generate_intermediates(realSourcePath);

		DirectoryInfo tempDir = new DirectoryInfo(Path.Combine(Path.GetFullPath(ProjectContext.CachePath), name));
		var files = tempDir.GetFiles();
		if (files is null) {
			throw new Exception($"Directory {tempDir.FullName} was empty");
		}

		var byExtension = files.GroupBy(f => f.Extension).ToDictionary(g => g.Key, g => g.ToList());

		var importedUids = new List<string>();

		// Meshes
		var meshUids = new Dictionary<string, string>();
		var meshes = byExtension.GetValueOrDefault(".tmesh") ?? [];
		log($"Importing {meshes.Count} meshes");
		foreach (var m in meshes) {
			var meshName = Path.GetFileNameWithoutExtension(m.Name);
			var destPath = Path.Combine(destDir, meshName + ".tmesh");
			var uid = UidGenerator.Generate();
			meshUids[meshName] = uid;

			log($"Mesh {meshName}");
			log($"Creating .tmesh file...");
			File.Copy(m.FullName, destPath, true);

			log("Writing .meta sidecar...");
			var header = new MetaHeader { Uid = uid, Type = AssetTypeRegistry.ByExtension(".tmesh")!.Type, Source = ctx.SourceVirtualPath };
			MetaFile.Write(destPath, header, m_settings.ToSection());
			importedUids.Add(uid);
		}

		// Textures
		var textureUids = new Dictionary<string, string>();
		if (m_settings.ImportMaterials) {
			var textures = (byExtension.GetValueOrDefault(".png") ?? [])
				.Concat(byExtension.GetValueOrDefault(".jpg") ?? [])
				.ToList();
			log($"Importing {textures.Count} textures...");
			foreach (var t in textures) {
				var texName = Path.GetFileNameWithoutExtension(t.Name);
				var destPath = Path.Combine(destDir, texName + ".ktx2");
				var uid = UidGenerator.Generate();
				textureUids[texName] = uid;

				log($"Texture {texName}");
				log("Converting to KTX2...");
				await KtxWriter.ConvertTexture(t.FullName, destPath, m_textureSettings, log);

				log("Generating thmbnail...");
				await Task.Run(() => ThumbnailService.Generate(t.FullName, uid));

				log("Writing .meta sidecar...");
				var header = new MetaHeader { Uid = uid, Type = AssetTypeRegistry.ByExtension(".ktx2")!.Type, Source = ctx.SourceVirtualPath };
				MetaFile.Write(destPath, header, m_textureSettings.ToSection(), m_settings.ToSection());
				importedUids.Add(uid);
			}
		}

		// Materials
		var materialUids = new Dictionary<string, string>();
		if (m_settings.ImportMaterials) {
			var materials = byExtension.GetValueOrDefault(".tmat") ?? [];
			log($"Importing {materials.Count} materials...");
			foreach (var m in materials) {
				var matName = Path.GetFileNameWithoutExtension(m.Name);
				var destPath = Path.Combine(destDir, matName + ".tmat");
				var uid = UidGenerator.Generate();
				materialUids[matName] = uid;

				log($"Material {matName}");
				log("Creating .tmat file...");
				var toml = await File.ReadAllTextAsync(m.FullName);
				foreach (var (texName, texUid) in textureUids) {
					toml = toml.Replace($"\"{texName}\"", $"\"{texUid}\"");
				}
				await File.WriteAllTextAsync(destPath, toml);

				log("Writing .meta sidecar...");
				var header = new MetaHeader { Uid = uid, Type = AssetTypeRegistry.ByExtension(".tmat")!.Type, Source = ctx.SourceVirtualPath };
				MetaFile.Write(destPath, header, m_settings.ToSection());
				importedUids.Add(uid);
			}
		}

		log("Rebuilding asset database...");
		AssetDatabase.RebuildAssetDatabase();

		// Scenes
		log("Updating scene intermediates with UIDs...");
		var scenes = byExtension.GetValueOrDefault(".json") ?? [];
		foreach (var s in scenes) {
			log($"Scene {Path.GetFileNameWithoutExtension(s.Name)}");

			var json = JsonNode.Parse(await File.ReadAllTextAsync(s.FullName))!;

			void PatchNode(JsonNode node) {
				if (node["type"]?.GetValue<string>() == "toast::MeshNode") {
					var p = node["params"]?.AsObject();
					if (p != null) {
						if (p["mesh"] is { } meshNode && meshUids.TryGetValue(meshNode.GetValue<string>(), out var meshUid))
							p["mesh"] = meshUid;
						if (p["material"] is { } matNode && materialUids.TryGetValue(matNode.GetValue<string>(), out var matUid))
							p["material"] = matUid;
					}
				}

				if (node["children"] is not JsonArray children) return;

				foreach (var child in children)
					if (child != null) PatchNode(child);
			}

			PatchNode(json);
			await File.WriteAllTextAsync(s.FullName, json.ToJsonString());
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
			var header = new MetaHeader { Uid = uid, Type = AssetTypeRegistry.ByExtension(".tnode")!.Type, Source = ctx.SourceVirtualPath };
			MetaFile.Write(destPath, header, m_settings.ToSection());
			importedUids.Add(uid);
		}

		log("Rebuilding asset database...");
		AssetDatabase.RebuildAssetDatabase();

		log("Removing intermediates...");
		if (Directory.Exists(tempDir.FullName)) Directory.Delete(tempDir.FullName, true);

		return importedUids;
	}

	public partial class Settings : ObservableObject {
		[ObservableProperty] private bool m_createSubfolder = true;
		[ObservableProperty] private bool m_importMaterials = true;
		[ObservableProperty] private bool m_importTextures = true;
		[ObservableProperty] private bool m_importCameras = false;
		[ObservableProperty] private bool m_importLights = true;
		[ObservableProperty] private bool m_generatePrefab = true;

		public GltfMetaSection ToSection() {
			return new GltfMetaSection {
				CreateFolder = CreateSubfolder,
				ImportMaterials = ImportMaterials,
				ImportTextures = ImportTextures,
				ImportCameras = ImportCameras,
				ImportLights = ImportLights,
				GeneratePrefab = GeneratePrefab
			};
		}
	}

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void gltf_generate_intermediates(string path);

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void gltf_create_tnode(string jsonPath, string outputPath);
}

