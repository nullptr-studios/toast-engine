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
using Tomlyn;

namespace editor.Assets.Importers;

public class FMODMetadata {
	public required string Schema { get; set; } = "EsFCxxgiok0";
	public required string Type { get; set; }
	public required string Name { get; set; }
	public required string Path { get; set; }
	public required string Guid { get; set; }
}

public partial class AudioStringImporter(AudioStringImporter.Settings settings) : IAssetImporter {
	private readonly Settings m_settings = settings;
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
		AssetTypeRegistry.ByExtension(".tae")!,
	];
	public IReadOnlyList<ImporterSetting> GetSettings() => [
		new ImporterSetting("Follow Folder Structure", SettingKind.Bool,
			() => m_settings.FollowFolderStructure,
			v => m_settings.FollowFolderStructure = (bool)v!),
		new ImporterSetting("Import Events", SettingKind.Bool,
			() => m_settings.ImportEvents,
			v => m_settings.ImportEvents = (bool)v!),
		new ImporterSetting("Import Buses", SettingKind.Bool,
			() => m_settings.ImportBuses,
			v => m_settings.ImportBuses = (bool)v!),
		new ImporterSetting("Import VCAs", SettingKind.Bool,
			() => m_settings.ImportVcas,
			v => m_settings.ImportVcas = (bool)v!),
		new ImporterSetting("Import Ports", SettingKind.Bool,
			() => m_settings.ImportPorts,
			v => m_settings.ImportPorts = (bool)v!),
		new ImporterSetting("Import Snapshots", SettingKind.Bool,
			() => m_settings.ImportSnapshots,
			v => m_settings.ImportSnapshots = (bool)v!),
	];

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
		var events = json["events"]?.AsObject() ?? new JsonObject();
		float total = 1 + vcas.Count + snapshots.Count + ports.Count + buses.Count + events.Count;
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

				if (type == "event" && m_settings.FollowFolderStructure) {
					var relativePath = path.Substring("event:/".Length);
					var outputPath = Path.Combine(ctx.DestDir, relativePath + ext);

					var directory = Path.GetDirectoryName(outputPath);
					if (!string.IsNullOrEmpty(directory))
						Directory.CreateDirectory(directory);

					destPath = outputPath;
				}
				else {
					destPath = Path.Combine(ctx.DestDir, $"{name}{ext}");
				}

				uid = ctx.UidFor(current);
				uids.Add(uid);
				header = new MetaHeader {
					Uid = uid,
					Type = AssetTypeRegistry.ByExtension($"{ext}")!.Type,
					Source = ctx.SourceVirtualPath
				};
				MetaFile.Write(destPath, header);

				var metadata = new FMODMetadata {
					Schema = "EsFCxxgiok0", // Holy magic number
					Type = type,
					Name = name,
					Path = path,
					Guid = guid,
				};

				string tomlString = TomlSerializer.Serialize(metadata);
				await File.WriteAllTextAsync(destPath, tomlString);

				progress?.Invoke(++current / total);
			}
		}

		if (m_settings.ImportEvents) {
			await ImportObjects(events, "event", ".tae");
		}

		if (m_settings.ImportVcas) {
			await ImportObjects(vcas, "vca", ".tvca");
		}

		if (m_settings.ImportBuses) {
			await ImportObjects(buses, "bus", ".tbus");
		}

		if (m_settings.ImportPorts) {
			await ImportObjects(ports, "port", ".taport");
		}

		if (m_settings.ImportSnapshots) {
			await ImportObjects(snapshots, "snapshot", ".tasnap");
		}
		
		Directory.Delete(Path.Combine(ProjectContext.CachePath, "fmod"), true);

		return uids;
	}

	[LibraryImport("toast_engine", StringMarshalling = StringMarshalling.Utf8)]
	private static partial void audio_generate_intermediates(string path);

	public partial class Settings : ObservableObject {
		[ObservableProperty] private bool m_followFolderStructure = true;
		[ObservableProperty] private bool m_importEvents = true;
		[ObservableProperty] private bool m_importBuses = true;
		[ObservableProperty] private bool m_importVcas = true;
		[ObservableProperty] private bool m_importPorts = true;
		[ObservableProperty] private bool m_importSnapshots = true;
		
		public AudioStringMetaSection ToSection() {
			return new AudioStringMetaSection {
				FollowFolderStructure = FollowFolderStructure,
				ImportEvents = ImportEvents,
				ImportBuses = ImportBuses,
				ImportVcas = ImportVcas,
				ImportPorts = ImportPorts,
				ImportSnapshots = ImportSnapshots
			};
		}
		
	}
}
