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
 * ParseSchema consumes. Only parameters tagged [Reflect] in the shader are
 * emitted
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
		var definitions = new JsonObject();

		AppendShaderParameters(properties, definitions, shaderUids);

		properties["settings"] = new JsonObject { ["x-toast-type"] = "MaterialSettings" };

		return BuildDocument(properties, definitions);
	}

	/// <summary>Builds the schema for a material instance</summary>
	public static string BuildForInstance(string parentMaterialUid) {
		var properties = new JsonObject {
			["material"] = new JsonObject { ["x-toast-type"] = "asset", ["x-toast-asset-type"] = "material" }
		};
		var definitions = new JsonObject();

		var parentTable = LoadMaterialTable(parentMaterialUid);
		if (parentTable != null) {
			var shaderUids = ReadShaderUids(parentTable);
			AppendShaderParameters(properties, definitions, shaderUids, parentTable);
			properties["settings"] = new JsonObject { ["x-toast-type"] = "MaterialSettings" };
		}

		return BuildDocument(properties, definitions);
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

	/// <summary>Inspector attribute block parsed from a reflection json node</summary>
	private sealed record InspectorMeta(
		bool Reflected, double? Min, double? Max, bool Color,
		string DisplayName, string Group, string Subgroup, string Unit) {
		public static InspectorMeta From(JsonNode? node) {
			if (node is not JsonObject o)
				return new InspectorMeta(false, null, null, false, "", "", "", "");
			return new InspectorMeta(
				o["reflected"]?.GetValue<bool>() ?? false,
				o["min"]?.GetValue<double>(),
				o["max"]?.GetValue<double>(),
				o["color"]?.GetValue<bool>() ?? false,
				o["display_name"]?.GetValue<string>() ?? "",
				o["group"]?.GetValue<string>() ?? "",
				o["subgroup"]?.GetValue<string>() ?? "",
				o["unit"]?.GetValue<string>() ?? "");
		}

		public string Label(string fallback) => DisplayName.Length > 0 ? DisplayName : fallback;
	}

	private static string BuildDocument(JsonObject properties, JsonObject definitions) {
		definitions["MaterialSettings"] = new JsonObject {
			["properties"] = new JsonObject {
				["blend_mode"] = WrapEnum(["opaque", "alpha", "additive", "multiply"], "opaque"),
				["depth_test"] = new JsonObject { ["x-toast-type"] = "bool", ["default"] = true },
				["depth_write"] = new JsonObject { ["x-toast-type"] = "bool", ["default"] = true },
				["cull_mode"] = WrapEnum(["none", "front", "back"], "back")
			}
		};

		var root = new JsonObject {
			["type"] = "object",
			["properties"] = properties,
			["definitions"] = definitions
		};
		return root.ToJsonString();
	}

	private static JsonObject WrapEnum(string[] options, string defaultValue) {
		var arr = new JsonArray();
		foreach (var opt in options) arr.Add(opt);
		return new JsonObject { ["x-toast-type"] = "enum", ["enum"] = arr, ["default"] = defaultValue };
	}

	private static JsonObject TextureDefProperties(TomlTable? textureDefaults) {
		var props = new JsonObject {
			["texture"] = new JsonObject { ["x-toast-type"] = "asset", ["x-toast-asset-type"] = "texture" },
			["repeat_u"] = WrapEnum(["repeat", "mirrored", "clamp", "border"], "repeat"),
			["repeat_v"] = WrapEnum(["repeat", "mirrored", "clamp", "border"], "repeat"),
			["min_filter"] = WrapEnum(["nearest", "linear"], "linear"),
			["mag_filter"] = WrapEnum(["nearest", "linear"], "linear"),
			["mipmap_mode"] = WrapEnum(["nearest", "linear"], "linear"),
			["anisotropy"] = new JsonObject { ["x-toast-type"] = "bool", ["default"] = true }
		};

		// Instances default their sampler fields to the parent material's values
		if (textureDefaults != null)
			foreach (var (key, value) in textureDefaults)
				if (props[key] is JsonObject prop && TomlValueToJson(value) is { } json)
					prop["default"] = json;

		return props;
	}

	/// <summary>
	/// Appends one schema property per [Reflect] shader parameter
	/// </summary>
	private static void AppendShaderParameters(
		JsonObject properties, JsonObject definitions, IReadOnlyList<string> shaderUids, TomlTable? defaults = null) {
		var seen = new HashSet<string>();

		var textureDefs = new Dictionary<string, JsonObject>();
		var textureByDisplay = new Dictionary<string, string>();
		var textureLabels = new Dictionary<string, string>();

		var shaderData = new List<(List<string> Order, Dictionary<string, JsonObject> Bindings, Dictionary<string, JsonObject> Pushes)>();

		// Gather reflected texture bindings so [Group]s can resolve to them
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

			shaderData.Add((order, bindingsByName, pushByName));

			foreach (var (name, binding) in bindingsByName) {
				var kind = binding["kind"]?.GetValue<string>() ?? "";
				if (kind is not ("combined_image_sampler" or "sampled_image")) continue;
				if (!string.IsNullOrEmpty(binding["engine_semantic"]?.GetValue<string>())) continue;
				var meta = InspectorMeta.From(binding["inspector"]);
				if (!meta.Reflected || textureDefs.ContainsKey(name)) continue;

				var texDefaults = defaults != null && defaults.TryGetValue(name, out var td) ? td as TomlTable : null;
				textureDefs[name] = TextureDefProperties(texDefaults);
				textureLabels[name] = meta.Label(name);
				if (meta.DisplayName.Length > 0) textureByDisplay[meta.DisplayName] = name;
			}
		}

		// Emits the texture property
		void EmitTexture(string name) {
			if (!seen.Add(name)) return;
			var defName = $"Tex_{name}";
			definitions[defName] = new JsonObject { ["properties"] = textureDefs[name] };
			properties[name] = new JsonObject {
				["x-toast-type"] = defName,
				["x-toast-display-name"] = textureLabels[name]
			};
		}

		// Finds the property bag a grouped member lands in
		(JsonObject Target, string GroupKey, string Subgroup) ResolveGroupTarget(InspectorMeta meta) {
			string groupKey;
			JsonObject target;

			if (textureDefs.ContainsKey(meta.Group) || textureByDisplay.ContainsKey(meta.Group)) {
				groupKey = textureDefs.ContainsKey(meta.Group) ? meta.Group : textureByDisplay[meta.Group];
				EmitTexture(groupKey);
				target = textureDefs[groupKey];
			} else {
				groupKey = meta.Group;
				var defName = $"Grp_{groupKey}";
				if (definitions[defName] is not JsonObject) {
					definitions[defName] = new JsonObject { ["properties"] = new JsonObject() };
					if (seen.Add(groupKey))
						properties[groupKey] = new JsonObject {
							["x-toast-type"] = defName,
							["x-toast-display-name"] = meta.Group
						};
				}
				target = (JsonObject)definitions[defName]!["properties"]!;
			}

			if (meta.Subgroup.Length == 0) return (target, groupKey, "");

			var subDefName = $"Grp_{groupKey}_{meta.Subgroup}";
			if (definitions[subDefName] is not JsonObject) {
				definitions[subDefName] = new JsonObject { ["properties"] = new JsonObject() };
				target[meta.Subgroup] = new JsonObject {
					["x-toast-type"] = subDefName,
					["x-toast-display-name"] = meta.Subgroup
				};
			}
			return ((JsonObject)definitions[subDefName]!["properties"]!, groupKey, meta.Subgroup);
		}

		// Walk each shader's layout order and emit properties
		foreach (var (order, bindingsByName, pushByName) in shaderData) {
			foreach (var paramName in order) {
				if (bindingsByName.TryGetValue(paramName, out var binding)) {
					if (!string.IsNullOrEmpty(binding["engine_semantic"]?.GetValue<string>())) continue;
					var kind = binding["kind"]?.GetValue<string>() ?? "";

					if (kind is "combined_image_sampler" or "sampled_image") {
						if (textureDefs.ContainsKey(paramName)) EmitTexture(paramName);
					} else if (kind == "uniform_buffer") {
						AppendBlockMembers(properties, binding["members"] as JsonArray, seen, defaults, ResolveGroupTarget);
					}
				} else if (pushByName.TryGetValue(paramName, out var push)) {
					AppendBlockMembers(properties, push["members"] as JsonArray, seen, defaults, ResolveGroupTarget);
				}
			}
		}
	}

	private static void AppendBlockMembers(
		JsonObject properties, JsonArray? members, HashSet<string> seen, TomlTable? defaults,
		Func<InspectorMeta, (JsonObject Target, string GroupKey, string Subgroup)> resolveGroup) {
		if (members is null) return;

		foreach (var member in members.OfType<JsonObject>()) {
			var name = member["name"]?.GetValue<string>() ?? "";
			if (name.Length == 0) continue;
			if (!string.IsNullOrEmpty(member["engine_semantic"]?.GetValue<string>())) continue;

			var meta = InspectorMeta.From(member["inspector"]);
			if (!meta.Reflected) continue;

			var isArray = (member["count"]?.GetValue<uint>() ?? 0) > 0;
			var toastType = MapMemberType(member["type"]?.GetValue<string>() ?? "", meta.Color);
			if (toastType is null) continue;
			if (isArray) toastType += "[]";

			var prop = new JsonObject {
				["x-toast-type"] = toastType,
				["x-toast-display-name"] = meta.Label(name)
			};
			if (meta.Unit.Length > 0) prop["x-toast-unit"] = meta.Unit;
			if (meta.Min.HasValue) prop["minimum"] = meta.Min.Value;
			if (meta.Max.HasValue) prop["maximum"] = meta.Max.Value;

			if (meta.Group.Length == 0) {
				if (!seen.Add(name)) continue;
				if (defaults != null && defaults.TryGetValue(name, out var def) && TomlValueToJson(def) is { } json)
					prop["default"] = json;
				properties[name] = prop;
			} else {
				var (target, groupKey, subgroup) = resolveGroup(meta);
				if (target.ContainsKey(name)) continue;
				if (LookupGroupedDefault(defaults, groupKey, subgroup, name) is { } grouped && TomlValueToJson(grouped) is { } json)
					prop["default"] = json;
				target[name] = prop;
			}
		}
	}

	private static object? LookupGroupedDefault(TomlTable? defaults, string groupKey, string subgroup, string name) {
		if (defaults is null) return null;
		if (!defaults.TryGetValue(groupKey, out var g) || g is not TomlTable groupTable) return null;

		var scope = groupTable;
		if (subgroup.Length > 0) {
			if (!groupTable.TryGetValue(subgroup, out var s) || s is not TomlTable subTable) return null;
			scope = subTable;
		}

		return scope.TryGetValue(name, out var value) ? value : null;
	}

	/// <summary>Reflection member type → x-toast-type; [Color] promotes vec3 to color3</summary>
	private static string? MapMemberType(string reflectionType, bool isColor) {
		return reflectionType switch {
			"float" => "float",
			"int" or "uint" => "int",
			"bool" => "bool",
			"vec2" => "vec2",
			"vec3" => isColor ? "color3" : "vec3",
			"vec4" => "color4", // DataValue has no plain vec4, color4 is the editable shape
			_ => null           // matrices etc are engine-written
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
