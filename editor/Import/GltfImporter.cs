using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using System.Linq;
using System.Text.Json.Nodes;
using editor.Services;
using editor.Workspace;

namespace editor.Import;

public class GltfImporter (GltfImportSettings gltfSettings, TextureImportSettings textureSettings) : IAssetImporter {
	public IReadOnlyList<string> SupportedExtensions => [".glb"];

	public async Task<IReadOnlyList<string>> Import(string realSourcePath, ImportContext ctx, Action<string> log) {
		var name = Path.GetFileNameWithoutExtension(realSourcePath);
		var destDir = ctx.DestDir;

		if(gltfSettings.CreateSubfolder) {
			destDir = Path.Combine(ctx.DestDir, name);
		}

		Directory.CreateDirectory(destDir);

		log($"Generating intermediates in cached://{name}...");
		ToastEngine.GltfGenerateIntermediates(realSourcePath);

		DirectoryInfo dir = new DirectoryInfo(Path.Combine(Path.GetFullPath(ProjectContext.CachePath), "name"));
		var files = dir.GetFiles();
		if (files is null) {
			throw new Exception($"Directory {dir.FullName} was empty");
		}

		var byExtension = files.GroupBy(f => f.Extension).ToDictionary(g => g.Key, g => g.ToList());

		// Meshes
		var meshUids = new Dictionary<string, string>();
		var meshes = byExtension[".tmesh"];
		log($"Importing {meshes.Count} meshes");
		foreach(var m in meshes) {
			var meshName = Path.GetFileNameWithoutExtension(m.Name);
			var destPath = Path.Combine(destDir, meshName + ".tmesh");
			var uid = UidGenerator.Generate();
			meshUids[meshName] = uid;

			log($"Mesh {meshName}");
			log($"Creating .tmesh file...");
			File.Copy(m.FullName, destPath, true);

			log("Writing .meta sidecar...");
			var header = new MetaHeader { Uid = uid, Type = "mesh", Source = ctx.SourceVirtualPath };
			MetaFile.Write(destPath, header, gltfSettings.ToSection());
		}

		// Textures
		var textureUids = new Dictionary<string, string>();
		if (gltfSettings.ImportMaterials) {
			var textures = byExtension[".png"];
			log($"Importing {textures.Count} textures...");
			foreach(var t in textures) {
				var texName = Path.GetFileNameWithoutExtension(t.Name);
				var destPath = Path.Combine(destDir, texName + ".ktx2");
				var uid = UidGenerator.Generate();
				textureUids[texName] = uid;

				log($"Texture {texName}");
				log("Converting to KTX2...");
				await KtxWriter.ConvertTexture(t.FullName, destPath, textureSettings, log);

				log("Generating thmbnail...");
				await Task.Run(() => ThumbnailService.Generate(t.FullName, uid));

				log("Writing .meta sidecar...");
				var header = new MetaHeader { Uid = uid, Type = "texture", Source = ctx.SourceVirtualPath };
				MetaFile.Write(destPath, header, textureSettings.ToSection(), gltfSettings.ToSection());
			}
		}

		// Materials
		var materialUids = new Dictionary<string, string>();
		if (gltfSettings.ImportMaterials) {
			var materials  = byExtension[".tmat"];
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
				var header = new MetaHeader { Uid = uid, Type = "material", Source = ctx.SourceVirtualPath };
				MetaFile.Write(destPath, header, gltfSettings.ToSection());
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
				if (node["type"]?.GetValue<string>() == "MeshNode") {
					var p = node["params"]?.AsObject();
					if (p != null) {
						if (p["mesh"] is { } meshNode && meshUids.TryGetValue(meshNode.GetValue<string>(), out var meshUid))
							p["mesh"] = meshUid;
						if (p["material"] is { } matNode && materialUids.TryGetValue(matNode.GetValue<string>(), out var matUid))
							p["material"] = matUid;
					}
				}
				if (node["children"] is JsonArray children) {
					foreach (var child in children)
						if (child != null) PatchNode(child);
				}
			}

			PatchNode(json);
			await File.WriteAllTextAsync(s.FullName, json.ToJsonString());
		}

		throw new NotImplementedException();
	}
}

