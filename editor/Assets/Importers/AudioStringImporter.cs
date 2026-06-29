using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using editor.Assets.Types;
using Lucide.Avalonia;
using Tomlyn;

namespace editor.Assets.Importers;

public class FMODMetadata {
	public required string Type { get; set; }
	public required string Name { get; set; }
	public required string Path { get; set; }
	public required string Guid { get; set; }
}

public partial class AudioStringImporter : IAssetImporter{
	public bool CanHandle(string filePath) => Path.GetFileName(filePath) == "Master.strings.bank";
	public IReadOnlyList<string> SupportedExtensions => [".strings.bank"];
	public string DisplayName => "Audio String";
	public LucideIconKind Icon => LucideIconKind.BookHeadphones;
	public BaseAsset PrimaryOutputType => AssetTypeRegistry.ByExtension(".strings.bank")!;
	public IReadOnlyList<BaseAsset> OutputTypes => [
		AssetTypeRegistry.ByExtension(".strings.bank")!,
		AssetTypeRegistry.ByExtension(".tbus")!,
		AssetTypeRegistry.ByExtension(".tvca")!,
		AssetTypeRegistry.ByExtension(".taport")!,
		AssetTypeRegistry.ByExtension(".tasnap")!,
	];
	public IReadOnlyList<ImporterSetting> GetSettings() => [];
	public async Task<IReadOnlyList<string>> Import(string realSourcePath, ImportContext ctx, Action<string> log, Action<double>? progress = null) {
		log("Generating intermediates...");
		Directory.CreateDirectory(ctx.DestDir);
		audio_generate_intermediates(realSourcePath);

		var json_path = Path.Combine(ProjectContext.CachePath, "fmod", "audio.json");
		if (!File.Exists(json_path)) {
			throw new FileNotFoundException("audio.json not found", Path.Combine(ProjectContext.CachePath, "audio.json"));
		}
		var json = JsonNode.Parse(await File.ReadAllTextAsync(json_path))!;
		var snapshots = json["snapshots"]?.AsObject() ?? new JsonObject();
		var vcas = json["vcas"]?.AsObject() ?? new JsonObject();
		var buses = json["buses"]?.AsObject() ?? new JsonObject();
		var ports = json["ports"]?.AsObject() ?? new JsonObject();
		float total = 1 + vcas.Count + snapshots.Count + ports.Count + buses.Count;
		int current = 0;
		List<string> uids = [];

		log("Importing strings bank...");
		var destPath = Path.Combine(ctx.DestDir, Path.GetFileName(realSourcePath));
		File.Copy(realSourcePath, destPath, true);
		var uid = ctx.UidFor(current);
		uids.Add(uid);
		var header = new MetaHeader { Uid = uid, Type = AssetTypeRegistry.ByType($"audio_strings")!.Type, Source = ctx.SourceVirtualPath };
		MetaFile.Write(destPath, header);
		progress?.Invoke(++current/total);

		async Task ImportObjects(JsonObject obj, string type, string ext) {
			log($"Importing {obj.Count} {type}s...");
			foreach (var (guid, value) in obj) {
				var path = value?.GetValue<string>() ?? "";
				if (string.IsNullOrEmpty(path)) {
					total--;
					continue;
				}
				log($"Importing {path}...");
			
				string name = path.Split('/').Last();
				if (path == "bus:/") {
					name = "Master Bus";
				}
				destPath = Path.Combine(ctx.DestDir, $"{name}{ext}");
				uid = ctx.UidFor(current);
				uids.Add(uid);
				header = new MetaHeader { Uid = uid, Type = AssetTypeRegistry.ByExtension($"{ext}")!.Type, Source = ctx.SourceVirtualPath };
				MetaFile.Write(destPath, header);

				// Create the object model
				var metadata = new FMODMetadata {
					Type = $"{type}",
					Name = name,
					Path = path,
					Guid = guid,
				};
				string tomlString = TomlSerializer.Serialize(metadata);
				await File.WriteAllTextAsync(destPath, tomlString);

				progress?.Invoke(++current / total);
			}
		}
		
		await ImportObjects(vcas, "vca", ".tvca");
		await ImportObjects(snapshots, "snapshot", ".tasnap");
		await ImportObjects(buses, "bus", ".tbus");
		await ImportObjects(ports, "port", ".taport");
		
		Directory.Delete(Path.Combine(ProjectContext.CachePath, "fmod"), true);

		return uids;
	}

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void audio_generate_intermediates(string path);
}
