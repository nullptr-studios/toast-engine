using System;
using System.Collections.Generic;
using System.IO;
using Tomlyn;
using Tomlyn.Serialization;

namespace editor.Services;

public record TextureMeta {
	public required string Uid { get; init; }
	public string Type { get; init; } = "texture";
	public required string Source { get; init; }
	public string CreatedAt { get; init; } = DateTime.UtcNow.ToString("o");
	public string ModifiedAt { get; set; } = DateTime.UtcNow.ToString("o");

	public bool GenerateMipmaps { get; init; } = true;
	public int MaxResolution { get; init; } = 4096;
	public string Compression { get; init; } = "BC7";
	public string SuperCompression { get; init; } = "Zstd";
	public string AddressU { get; init; } = "Repeat";
	public string AddressV { get; init; } = "Repeat";
	public string Filter { get; init; } = "Trilinear";
	public float Anisotropy { get; init; } = 8.0f;
}

public static class MetaFile {
	// Writes outputAssetRealPath + ".meta"
	public static void Write(string outputAssetRealPath, TextureMeta meta) {
		var dto = new MetaFileDto {
			Uid = meta.Uid,
			Type = meta.Type,
			Source = meta.Source,
			CreatedAt = meta.CreatedAt,
			ModifiedAt = meta.ModifiedAt,
			Texture = new TextureSectionDto {
				GenerateMipmaps = meta.GenerateMipmaps,
				MaxResolution = meta.MaxResolution,
				Compression = meta.Compression,
				SuperCompression = meta.SuperCompression,
				AddressU = meta.AddressU,
				AddressV = meta.AddressV,
				Filter = meta.Filter,
				Anisotropy = meta.Anisotropy
			}
		};
		File.WriteAllText(outputAssetRealPath + ".meta", TomlSerializer.Serialize(dto));
	}

	public static TextureMeta? ReadTexture(string metaPath) {
		if (!File.Exists(metaPath)) return null;
		try {
			var dto = TomlSerializer.Deserialize<MetaFileDto>(File.ReadAllText(metaPath))!;
			var t = dto.Texture ?? new TextureSectionDto();
			return new TextureMeta {
				Uid = dto.Uid,
				Type = dto.Type,
				Source = dto.Source,
				CreatedAt = dto.CreatedAt,
				ModifiedAt = dto.ModifiedAt,
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

	public static IEnumerable<string> FindAll(string directory) {
		return Directory.EnumerateFiles(directory, "*.meta", SearchOption.AllDirectories);
	}
}

file sealed class MetaFileDto {
	[TomlPropertyName("uid")] public string Uid { get; set; } = "";
	[TomlPropertyName("type")] public string Type { get; set; } = "texture";
	[TomlPropertyName("source")] public string Source { get; set; } = "";
	[TomlPropertyName("created_at")] public string CreatedAt { get; set; } = "";
	[TomlPropertyName("modified_at")] public string ModifiedAt { get; set; } = "";

	// Section must be declared last so its [texture] header follows the root
	// key/values, as required by TOML.
	[TomlPropertyName("texture")] public TextureSectionDto? Texture { get; set; }
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
