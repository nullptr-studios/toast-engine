using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using Tomlyn;
using Tomlyn.Model;

namespace editor.Assets;

/**
 * <summary>
 * Generates GenericEditor schemas for materials from shader reflection data
 *
 * The engine compiles every shader to cache://shaders/\<uid\>.json
 * This builder turns those reflection files into the same JSON-schema shape
 * ParseSchema consumes, so a material's editable fields always mirror its
 * shaders' uniforms, push constants and textures
 * </summary>
 */
public static class MaterialSchemaGenerator {
	/// <summary>Builds the schema for a material, driven by its shaders array</summary>
	public static string BuildForShaders(IReadOnlyList<string> shaderUids) {
		var properties = new JsonObject {
			["name"] = new JsonObject { ["x-toast-type"] = "string", ["default"] = "Unnamed Material" },
			["shaders"] = new JsonObject {
				["type"] = "array",
				["x-toast-type"] = "asset[]",
				["x-toast-asset-type"] = "shader"
			}
		};

		AppendShaderParameters(properties, shaderUids);

		properties["settings"] = new JsonObject { ["x-toast-type"] = "MaterialSettings" };

		return BuildDocument(properties);
	}

	/// <summary>Builds the schema for a material instance</summary>
	public static string BuildForInstance(string parentMaterialUid) {
		var properties = new JsonObject {
			["material"] = new JsonObject { ["x-toast-type"] = "asset", ["x-toast-asset-type"] = "material" }
		};

		var parentTable = LoadMaterialTable(parentMaterialUid);
		if (parentTable != null) {
			var shaderUids = ReadShaderUids(parentTable);
			AppendShaderParameters(properties, shaderUids, parentTable);
			properties["settings"] = new JsonObject { ["x-toast-type"] = "MaterialSettings" };
		}

		return BuildDocument(properties);
	}

	/// <summary>Reads a material's TOML by asset uid</summary>
	public static TomlTable? LoadMaterialTable(string materialUid) {
		if (string.IsNullOrEmpty(materialUid)) return null;
		if (!AssetDatabase.TryResolve(materialUid, out var virtualPath, out _)) return null;
		var realPath = ProjectContext.Resolve(virtualPath);
		if (!File.Exists(realPath)) return null;
		try {
			return TomlSerializer.Deserialize<TomlTable>(File.ReadAllText(realPath));
		} catch {
			return null;
		}
	}

	/// <summary>Extracts the shaders uid array out of a material TOML table</summary>
	public static List<string> ReadShaderUids(TomlTable table) {
		var result = new List<string>();
		if (table.TryGetValue("shaders", out var shaders) && shaders is TomlArray arr)
			foreach (var item in arr)
				if (item?.ToString() is { Length: 11 } uid)
					result.Add(uid);
		return result;
	}

	private static string BuildDocument(JsonObject properties) {
		var root = new JsonObject {
			["type"] = "object",
			["properties"] = properties,
			["definitions"] = new JsonObject {
				["TextureParameter"] = new JsonObject {
					["properties"] = new JsonObject {
						["texture"] = new JsonObject { ["x-toast-type"] = "asset", ["x-toast-asset-type"] = "texture" },
						["repeat_u"] = WrapEnum(["repeat", "mirrored", "clamp", "border"], "repeat"),
						["repeat_v"] = WrapEnum(["repeat", "mirrored", "clamp", "border"], "repeat"),
						["min_filter"] = WrapEnum(["nearest", "linear"], "linear"),
						["mag_filter"] = WrapEnum(["nearest", "linear"], "linear"),
						["mipmap_mode"] = WrapEnum(["nearest", "linear"], "linear"),
						["anisotropy"] = new JsonObject { ["x-toast-type"] = "bool", ["default"] = true }
					}
				},
				["MaterialSettings"] = new JsonObject {
					["properties"] = new JsonObject {
						["blend_mode"] = WrapEnum(["opaque", "alpha", "additive", "multiply"], "opaque"),
						["depth_test"] = new JsonObject { ["x-toast-type"] = "bool", ["default"] = true },
						["depth_write"] = new JsonObject { ["x-toast-type"] = "bool", ["default"] = true },
						["cull_mode"] = WrapEnum(["none", "front", "back"], "back")
					}
				}
			}
		};
		return root.ToJsonString();
	}

	private static JsonObject WrapEnum(string[] options, string defaultValue) {
		var arr = new JsonArray();
		foreach (var opt in options) arr.Add(opt);
		return new JsonObject { ["x-toast-type"] = "enum", ["enum"] = arr, ["default"] = defaultValue };
	}

	/// <summary>Appends one schema property per shader parameter</summary>
	private static void AppendShaderParameters(
		JsonObject properties, IReadOnlyList<string> shaderUids, TomlTable? defaults = null) {
		var seen = new HashSet<string>();

		foreach (var uid in shaderUids) {
			var reflection = LoadReflection(uid);
			if (reflection is null) continue;

			var bindingsByName = new Dictionary<string, JsonObject>();
			if (reflection["bindings"] is JsonArray bindings)
				foreach (var b in bindings.OfType<JsonObject>())
					if (b["name"]?.GetValue<string>() is { Length: > 0 } n)
						bindingsByName[n] = b;

			var pushByName = new Dictionary<string, JsonObject>();
			if (reflection["push_constants"] is JsonArray pushes)
				foreach (var p in pushes.OfType<JsonObject>())
					if (p["name"]?.GetValue<string>() is { Length: > 0 } n)
						pushByName[n] = p;

			var order = new List<string>();
			if (reflection["layout_order"] is JsonArray layoutOrder)
				foreach (var entry in layoutOrder)
					if (entry?.GetValue<string>() is { } n)
						order.Add(n);

			foreach (var paramName in order) {
				if (bindingsByName.TryGetValue(paramName, out var binding)) {
					if (!string.IsNullOrEmpty(binding["engine_semantic"]?.GetValue<string>())) continue;
					var kind = binding["kind"]?.GetValue<string>() ?? "";

					if (kind is "combined_image_sampler" or "sampled_image") {
						if (seen.Add(paramName))
							properties[paramName] = new JsonObject { ["x-toast-type"] = "TextureParameter" };
					} else if (kind == "uniform_buffer") {
						AppendBlockMembers(properties, binding["members"] as JsonArray, seen, defaults);
					}
				} else if (pushByName.TryGetValue(paramName, out var push)) {
					AppendBlockMembers(properties, push["members"] as JsonArray, seen, defaults);
				}
			}
		}
	}

	private static void AppendBlockMembers(
		JsonObject properties, JsonArray? members, HashSet<string> seen, TomlTable? defaults) {
		if (members is null) return;

		foreach (var member in members.OfType<JsonObject>()) {
			var name = member["name"]?.GetValue<string>() ?? "";
			if (name.Length == 0 || !seen.Add(name)) continue;
			if (!string.IsNullOrEmpty(member["engine_semantic"]?.GetValue<string>())) continue;

			var toastType = MapMemberType(member["type"]?.GetValue<string>() ?? "", name);
			if (toastType is null) continue;

			var prop = new JsonObject { ["x-toast-type"] = toastType };
			if (defaults != null && defaults.TryGetValue(name, out var def))
				prop["default"] = TomlValueToJson(def);
			properties[name] = prop;
		}
	}

	/// <summary>Reflection member type → x-toast-type</summary>
	private static string? MapMemberType(string reflectionType, string memberName) {
		var looksLikeColor = memberName.Contains("color", StringComparison.OrdinalIgnoreCase) ||
		                     memberName.Contains("tint", StringComparison.OrdinalIgnoreCase);
		return reflectionType switch {
			"float" => "float",
			"int" or "uint" => "int",
			"bool" => "bool",
			"vec2" => "vec2",
			"vec3" => looksLikeColor ? "color3" : "vec3",
			"vec4" => "color4",
			_ => null // matrices etc are engine-written
		};
	}

	private static JsonNode? TomlValueToJson(object? value) {
		return value switch {
			bool b => JsonValue.Create(b),
			long l => JsonValue.Create(l),
			double d => JsonValue.Create(d),
			string s => JsonValue.Create(s),
			TomlArray arr => new JsonArray(arr.Select(TomlValueToJson).ToArray()),
			_ => null
		};
	}

	private static JsonObject? LoadReflection(string shaderUid) {
		try {
			var path = ProjectContext.Resolve($"cache://shaders/{shaderUid}.json");
			if (!File.Exists(path)) return null;
			var root = JsonNode.Parse(File.ReadAllText(path))?.AsObject();
			return root?["reflection"] as JsonObject;
		} catch {
			return null;
		}
	}
}
