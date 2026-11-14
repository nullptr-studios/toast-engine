using ReflectionGenerator.Model;

namespace ReflectionGenerator.Rules;

// every c++ to lua type decision lives here with one function per place a type shows up
internal static class LuaTypes
{
	public static string ForField(Field field)
	{
		var color = field.Attributes.Has("Color");
		var type = field.Kind switch {
			FieldKind.Bool => "boolean",
			FieldKind.Int => "integer",
			FieldKind.Float or FieldKind.Double => "number",
			FieldKind.String => "string",
			FieldKind.Vec2 => "vec2",
			FieldKind.Vec3 => color ? "color3" : "vec3",
			FieldKind.Vec4 => color ? "color4" : "vec4",
			FieldKind.Quaternion => "quat",
			FieldKind.Uid => ForHandleOrBox(field.TypeName),
			_ => "any",
		};
		return field.IsArray ? type + "[]" : type;
	}

	private static string ForHandleOrBox(string typeName)
	{
		var inner = typeName.TrimStartText("std::vector<").TrimEnd('>');
		if (inner.StartsWith("Box<", StringComparison.Ordinal)) {
			return inner["Box<".Length..].TrimEnd('>').Bare();
		}
		return inner.Contains("Handle<") ? "Asset" : "any";
	}

	// parameters, return values and signal arguments
	public static string ForCppType(string cpp)
	{
		var type = cpp.Trim();

		if (Unwrap(type, "std::optional<") is { } optional) return ForCppType(optional) + "|nil";
		if (Unwrap(type, "std::vector<") is { } vector) return ForCppType(vector) + "[]";

		// the longer names go first because the match is a substring one
		string[] knownNames = ["input::ActionEvent", "input::Action", "input::Bind", "input::KeyCode", "input::Device",
			"input::ValueType", "input::ModifierKey", "input::InputKind", "voxel::WriteMode"];
		string[] luaNames = ["InputActionEvent", "InputAction", "InputBind", "InputKeyCode", "InputDeviceValue",
			"InputValueTypeValue", "InputModifierValue", "InputKindValue", "VoxelWriteValue"];
		for (var i = 0; i < knownNames.Length; i++) {
			if (type.Contains(knownNames[i])) return luaNames[i];
		}

		if (type.Contains("Box<")) {
			var inner = type.Split("Box<").ElementAtOrDefault(1) ?? "Node";
			return inner.TrimEnd('>', '&', ' ').Bare();
		}
		if (type.Contains("Handle<")) return "Asset";
		if (type.Contains("physics::RayHit")) return "RayHit";
		if (type.Contains("bool")) return "boolean";
		if (type.Contains("float") || type.Contains("double")) return "number";
		if (type.Contains("string")) return "string";
		if (type.Contains("vec4")) return "vec4";
		if (type.Contains("vec3")) return "vec3";
		if (type.Contains("vec2")) return "vec2";
		if (type.Contains("quat")) return "quat";
		if (type == "void") return "nil";
		if (type.Contains("int") || type.Contains("long") || type.Contains("short") || type == "char") return "integer";
		return "any";
	}

	private static string? Unwrap(string type, string prefix) =>
		type.StartsWith(prefix, StringComparison.Ordinal) && type.EndsWith('>') ? type[prefix.Length..^1] : null;

	public static string ForMethod(string className, Method method)
	{
		var parameters = new List<string> { $"self: {className}" };
		foreach (var parameter in method.Parameters) {
			var optional = parameter.Default is null ? "" : "?";
			parameters.Add($"{parameter.Name}{optional}: {ForCppType(parameter.Type)}");
		}

		var returns = ForCppType(method.ReturnType);
		var signature = $"fun({string.Join(", ", parameters)})";
		return returns == "nil" ? signature : $"{signature}: {returns}";
	}

	// the lua side only has Signal0 to Signal4
	public static string ForSignal(Signal signal)
	{
		var arguments = signal.Arguments.Select(ForCppType).ToList();
		return arguments.Count is >= 1 and <= 4
			? $"Signal{arguments.Count}<{string.Join(", ", arguments)}>"
			: "Signal0";
	}

	// null means lua cannot see the type and readable false means lua cannot send it
	public static (string Lua, bool Readable)? ForEventField(string cpp, IReadOnlyCollection<string> enumNames)
	{
		var bare = cpp.WithoutWhitespace().TrimStartText("const").TrimStartText("std::").TrimStartText("glm::");

		if (Unwrap(bare, "vector<") is { } element) {
			return ForEventField(element, enumNames) is { } inner ? (inner.Lua + "[]", inner.Readable) : null;
		}
		if (bare.StartsWith("assets::Handle<", StringComparison.Ordinal) || bare.StartsWith("Handle<", StringComparison.Ordinal)) {
			return ("Asset", true);
		}
		if (enumNames.Contains(bare.Bare())) return ("integer", true);

		return bare switch {
			"toast::Box<toast::Node>" or "Box<toast::Node>" or "toast::Box<Node>" or "Box<Node>"
				or "toast::Node*" or "Node*" => ("Node", true),
			"bool" => ("boolean", true),
			"float" or "double" => ("number", true),
			"char" or "short" or "int" or "long" or "longlong" or "unsigned" or "unsignedint"
				or "unsignedlong" or "unsignedlonglong" or "int8_t" or "int16_t" or "int32_t" or "int64_t"
				or "uint8_t" or "uint16_t" or "uint32_t" or "uint64_t" or "size_t" => ("integer", true),
			"string" => ("string", true),
			"vec2" or "ivec2" => ("vec2", true),
			"vec3" or "ivec3" => ("vec3", true),
			"vec4" or "ivec4" => ("vec4", true),
			"quat" or "quaternion" => ("quat", true),
			"toast::UID" or "UID" => ("UID", true),
			"input::Device" => ("InputDeviceValue", true),
			"input::ActionEvent" => ("InputActionEvent", true),
			"input::Action&" or "input::Action" => ("InputAction", false),
			"ContactEventData" or "event::ContactEventData" => ("ContactEventData", false),
			"physics::BroadPhasePair" or "BroadPhasePair" => ("BroadPhasePair", false),
			_ => null,
		};
	}
}
