using System;
using System.Collections.Generic;
using System.IO;
using Tomlyn;
using Tomlyn.Serialization;

namespace editor.Services;

public interface IMetaSection { }

public record MetaHeader {
	public required string Uid { get; init; }
	public required string Type { get; init; }
	public string? Source { get; init; }
	public string CreatedAt { get; init; } = DateTime.UtcNow.ToString("o");
	public string ModifiedAt { get; set; } = DateTime.UtcNow.ToString("o");
}

public record TextureMetaSection : IMetaSection {
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
	public bool CreateFolder { get; init; } = false;
}

public static class MetaFile {
	public static void Write(string outputAssetRealPath, MetaHeader header, params IMetaSection[] sections) {
		var dto = new MetaFileDto {
			Uid = header.Uid,
			Type = header.Type,
			Source = header.Source,
			CreatedAt = header.CreatedAt,
			ModifiedAt = header.ModifiedAt
		};

		foreach (var section in sections) {
			switch (section) {
				case TextureMetaSection texture:
					dto.Texture = new TextureSectionDto {
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
			}
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
				ModifiedAt = dto.ModifiedAt
			};
		} catch {
			return null;
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

	public static IEnumerable<string> FindAll(string directory) {
		return Directory.EnumerateFiles(directory, "*.meta", SearchOption.AllDirectories);
	}
}

file sealed class MetaFileDto {
	[TomlPropertyName("uid")] public string Uid { get; set; } = "";
	[TomlPropertyName("type")] public string Type { get; set; } = "";
	// Source is omitted from the TOML output when assets have no artwork source
	[TomlPropertyName("source")] public string? Source { get; set; }
	[TomlPropertyName("created_at")] public string CreatedAt { get; set; } = "";
	[TomlPropertyName("modified_at")] public string ModifiedAt { get; set; } = "";

	[TomlPropertyName("texture")] public TextureSectionDto? Texture { get; set; }
	[TomlPropertyName("psd")] public PsdSectionDto? Psd { get; set; }
}

file sealed class TextureSectionDto {
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
	[TomlPropertyName("create_folder")] public bool CreateFolder { get; set; } = false;
}
