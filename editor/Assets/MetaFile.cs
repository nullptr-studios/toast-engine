using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Tomlyn;
using Tomlyn.Model;
using Tomlyn.Serialization;

namespace editor.Assets;

public interface IMetaSection { }

public record MetaHeader {
	public required string Uid { get; init; }
	public required string Type { get; init; }
	public string? Source { get; init; }
	public string CreatedAt { get; init; } = DateTime.UtcNow.ToString("o");
	public string ModifiedAt { get; set; } = DateTime.UtcNow.ToString("o");
    public IReadOnlyList<string>? Tags { get; init; }
}

public record TextureMetaSection : IMetaSection {
	public string ColorSpace { get; init; } = "sRGB";
	public bool GenerateMipmaps { get; init; } = true;
	public int MaxResolution { get; init; } = 4096;
	public string Compression { get; init; } = "BC7";
	public string SuperCompression { get; init; } = "Zstd";
	public string AddressU { get; init; } = "Repeat";
	public string AddressV { get; init; } = "Repeat";
	public string Filter { get; init; } = "Trilinear";
	public float Anisotropy { get; init; } = 8.0f;
}

public record PsdMetaSection : IMetaSection {
	public string ImportMode { get; init; } = "Combined";
	public bool CreateFolder { get; init; }
}

public record GltfMetaSection : IMetaSection {
	public bool CreateFolder { get; init; } = true;
	public bool ImportMaterials { get; init; } = true;
	public bool ImportTextures { get; init; } = true;
	public bool ImportCameras { get; init; }
	public bool ImportLights { get; init; } = true;
	public bool ImportAnimations { get; init; } = true;
	public bool GeneratePrefab { get; init; } = true;
}

public record VoxMetaSection : IMetaSection {
	public bool CreateFolder { get; init; } = true;
	public bool ImportPalette { get; init; } = true;
	public bool GeneratePrefab { get; init; } = true;
}

public record AudioStringMetaSection : IMetaSection {
	public bool FollowFolderStructure = true;
	public bool ImportBuses = true;
	public bool ImportEvents = true;
	public bool ImportPorts = true;
	public bool ImportSnapshots = true;
	public bool ImportVcas = true;
}

public static class MetaFile {
	public static void Write(string outputAssetRealPath, MetaHeader header, params IMetaSection[] sections) {
		var tags = header.Tags ?? ReadHeader(outputAssetRealPath)?.Tags;
		var dto = new MetaFileDto {
			Uid = header.Uid,
			Type = header.Type,
			Source = header.Source,
			CreatedAt = header.CreatedAt,
			ModifiedAt = header.ModifiedAt,
			Tags = tags is { Count: > 0 } ? tags.ToList() : null
		};

		foreach (var section in sections)
			switch (section) {
				case TextureMetaSection texture:
					dto.Texture = new TextureSectionDto {
						ColorSpace = texture.ColorSpace,
						GenerateMipmaps = texture.GenerateMipmaps,
						MaxResolution = texture.MaxResolution,
						Compression = texture.Compression,
						SuperCompression = texture.SuperCompression,
						AddressU = texture.AddressU,
						AddressV = texture.AddressV,
						Filter = texture.Filter,
						Anisotropy = texture.Anisotropy
					};
					break;
				case PsdMetaSection psd:
					dto.Psd = new PsdSectionDto {
						ImportMode = psd.ImportMode,
						CreateFolder = psd.CreateFolder
					};
					break;
				case VoxMetaSection vox:
					dto.Vox = new VoxSectionDto {
						CreateFolder = vox.CreateFolder,
						ImportPalette = vox.ImportPalette,
						GeneratePrefab = vox.GeneratePrefab
					};
					break;
				case GltfMetaSection gltf:
					dto.Gltf = new GltfSectionDto {
						CreateFolder = gltf.CreateFolder,
						ImportMaterials = gltf.ImportMaterials,
						ImportTextures = gltf.ImportTextures,
						ImportCameras = gltf.ImportCameras,
						ImportLights = gltf.ImportLights,
						ImportAnimations = gltf.ImportAnimations,
						GeneratePrefab = gltf.GeneratePrefab
					};
					break;
			}

		File.WriteAllText(outputAssetRealPath + ".meta", TomlSerializer.Serialize(dto));
	}

	public static MetaHeader? ReadHeader(string path) {
		var metaPath = path.EndsWith(".meta") ? path : path + ".meta";
		if (!File.Exists(metaPath)) return null;
		try {
			var dto = TomlSerializer.Deserialize<MetaFileDto>(File.ReadAllText(metaPath))!;
			return new MetaHeader {
				Uid = dto.Uid,
				Type = dto.Type,
				Source = dto.Source,
				CreatedAt = dto.CreatedAt,
				ModifiedAt = dto.ModifiedAt,
				Tags = dto.Tags
			};
		} catch {
			return null;
		}
	}

	public static bool SetTags(string path, IReadOnlyCollection<string> tagIds) {
		var metaPath = path.EndsWith(".meta") ? path : path + ".meta";
		if (!File.Exists(metaPath)) return false;
		try {
			var table = TomlSerializer.Deserialize<TomlTable>(File.ReadAllText(metaPath))!;
			if (tagIds.Count == 0) {
				table.Remove("tags");
			} else {
				var array = new TomlArray();
				foreach (var id in tagIds) array.Add(id);
				table["tags"] = array;
			}

			File.WriteAllText(metaPath, TomlSerializer.Serialize(table));
			return true;
		} catch {
			return false;
		}
	}

	public static TextureMetaSection? ReadTextureSection(string path) {
		var metaPath = path.EndsWith(".meta") ? path : path + ".meta";
		if (!File.Exists(metaPath)) return null;
		try {
			var dto = TomlSerializer.Deserialize<MetaFileDto>(File.ReadAllText(metaPath))!;
			if (dto.Texture == null) return null;
			var t = dto.Texture;
			return new TextureMetaSection {
				ColorSpace = t.ColorSpace,
				GenerateMipmaps = t.GenerateMipmaps,
				MaxResolution = t.MaxResolution,
				Compression = t.Compression,
				SuperCompression = t.SuperCompression,
				AddressU = t.AddressU,
				AddressV = t.AddressV,
				Filter = t.Filter,
				Anisotropy = t.Anisotropy
			};
		} catch {
			return null;
		}
	}

	public static PsdMetaSection? ReadPsdSection(string path) {
		var metaPath = path.EndsWith(".meta") ? path : path + ".meta";
		if (!File.Exists(metaPath)) return null;
		try {
			var dto = TomlSerializer.Deserialize<MetaFileDto>(File.ReadAllText(metaPath))!;
			if (dto.Psd == null) return null;
			return new PsdMetaSection {
				ImportMode = dto.Psd.ImportMode,
				CreateFolder = dto.Psd.CreateFolder
			};
		} catch {
			return null;
		}
	}

	public static VoxMetaSection? ReadVoxSection(string path) {
		var metaPath = path.EndsWith(".meta") ? path : path + ".meta";
		if (!File.Exists(metaPath)) return null;
		try {
			var dto = TomlSerializer.Deserialize<MetaFileDto>(File.ReadAllText(metaPath))!;
			if (dto.Vox == null) return null;
			return new VoxMetaSection {
				CreateFolder = dto.Vox.CreateFolder,
				ImportPalette = dto.Vox.ImportPalette,
				GeneratePrefab = dto.Vox.GeneratePrefab
			};
		} catch {
			return null;
		}
	}

	public static GltfMetaSection? ReadGltfSection(string path) {
		var metaPath = path.EndsWith(".meta") ? path : path + ".meta";
		if (!File.Exists(metaPath)) return null;
		try {
			var dto = TomlSerializer.Deserialize<MetaFileDto>(File.ReadAllText(metaPath))!;
			if (dto.Gltf == null) return null;
			return new GltfMetaSection {
				CreateFolder = dto.Gltf.CreateFolder,
				ImportMaterials = dto.Gltf.ImportMaterials,
				ImportTextures = dto.Gltf.ImportTextures,
				ImportCameras = dto.Gltf.ImportCameras,
				ImportLights = dto.Gltf.ImportLights,
				ImportAnimations = dto.Gltf.ImportAnimations,
				GeneratePrefab = dto.Gltf.GeneratePrefab
			};
		} catch {
			return null;
		}
	}

	/// Rewrites the source of an existing .meta in place
	public static bool UpdateSource(string path, string? newSource) {
		var metaPath = path.EndsWith(".meta") ? path : path + ".meta";
		if (!File.Exists(metaPath)) return false;
		try {
			var dto = TomlSerializer.Deserialize<MetaFileDto>(File.ReadAllText(metaPath))!;
			dto.Source = newSource;
			dto.ModifiedAt = DateTime.UtcNow.ToString("o");
			File.WriteAllText(metaPath, TomlSerializer.Serialize(dto));
			return true;
		} catch {
			return false;
		}
	}

	/// Rewrites the modified_at of an existing .meta in place;
	public static bool Touch(string virtualPath) {
		var metaPath = ProjectContext.Resolve(virtualPath);
		if (!metaPath.EndsWith(".meta")) metaPath += ".meta";
		if (!File.Exists(metaPath)) return false;
		try {
			var dto = TomlSerializer.Deserialize<MetaFileDto>(File.ReadAllText(metaPath))!;
			dto.ModifiedAt = DateTime.UtcNow.ToString("o");
			File.WriteAllText(metaPath, TomlSerializer.Serialize(dto));
			return true;
		} catch {
			return false;
		}
	}

	public static IEnumerable<string> FindAll(string directory) {
		return Directory.EnumerateFiles(directory, "*.meta", SearchOption.AllDirectories);
	}
}

file sealed class MetaFileDto {
	[TomlPropertyName("uid")] public string Uid { get; set; } = "";
	[TomlPropertyName("type")] public string Type { get; set; } = "";
	[TomlPropertyName("source")] public string? Source { get; set; }
	[TomlPropertyName("created_at")] public string CreatedAt { get; set; } = "";
	[TomlPropertyName("modified_at")] public string ModifiedAt { get; set; } = "";
	[TomlPropertyName("tags")] public List<string>? Tags { get; set; }

	[TomlPropertyName("texture")] public TextureSectionDto? Texture { get; set; }
	[TomlPropertyName("psd")] public PsdSectionDto? Psd { get; set; }
	[TomlPropertyName("gltf")] public GltfSectionDto? Gltf { get; set; }
	[TomlPropertyName("vox")] public VoxSectionDto? Vox { get; set; }
}

file sealed class VoxSectionDto {
	[TomlPropertyName("create_folder")] public bool CreateFolder { get; set; } = true;
	[TomlPropertyName("import_palette")] public bool ImportPalette { get; set; } = true;
	[TomlPropertyName("generate_prefab")] public bool GeneratePrefab { get; set; } = true;
}

file sealed class TextureSectionDto {
	[TomlPropertyName("color_space")] public string ColorSpace { get; set; } = "sRGB";
	[TomlPropertyName("generate_mipmaps")] public bool GenerateMipmaps { get; set; } = true;
	[TomlPropertyName("max_resolution")] public int MaxResolution { get; set; } = 4096;
	[TomlPropertyName("compression")] public string Compression { get; set; } = "BC7";

	[TomlPropertyName("super_compression")]
	public string SuperCompression { get; set; } = "Zstd";

	[TomlPropertyName("address_u")] public string AddressU { get; set; } = "Repeat";
	[TomlPropertyName("address_v")] public string AddressV { get; set; } = "Repeat";
	[TomlPropertyName("filter")] public string Filter { get; set; } = "Trilinear";
	[TomlPropertyName("anisotropy")] public float Anisotropy { get; set; } = 8.0f;
}

file sealed class PsdSectionDto {
	[TomlPropertyName("import_mode")] public string ImportMode { get; set; } = "Combined";
	[TomlPropertyName("create_folder")] public bool CreateFolder { get; set; }
}

file sealed class GltfSectionDto {
	[TomlPropertyName("create_folder")] public bool CreateFolder { get; set; } = true;
	[TomlPropertyName("import_materials")] public bool ImportMaterials { get; set; } = true;
	[TomlPropertyName("import_textures")] public bool ImportTextures { get; set; } = true;
	[TomlPropertyName("import_cameras")] public bool ImportCameras { get; set; }
	[TomlPropertyName("import_lights")] public bool ImportLights { get; set; } = true;

	[TomlPropertyName("import_animations")]
	public bool ImportAnimations { get; set; } = true;
	[TomlPropertyName("generate_prefab")] public bool GeneratePrefab { get; set; } = true;
}
