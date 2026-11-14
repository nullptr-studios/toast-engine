using ReflectionGenerator.Model;
using ReflectionGenerator.Parsing;

namespace ReflectionGenerator.Rules;

internal static class FieldKinds
{
	public static FieldKind Infer(string typeName)
	{
		var trimmed = typeName.Trim();

		// classify a vector by its element or save throws bad_any_cast on std::vector<std::string>
		var element = trimmed.Contains("vector<")
			? TemplateArgs.Split(trimmed).FirstOrDefault() ?? ""
			: trimmed;

		var bare = element.Trim().TrimStartText("toast::").TrimStartText("std::").TrimStartText("glm::");

		if (element.Contains("Handle<")) return FieldKind.Uid;
		if (bare.StartsWith("Box<", StringComparison.Ordinal)) return FieldKind.Uid;

		return bare switch {
			"bool" => FieldKind.Bool,
			"int" or "int8_t" or "int16_t" or "int32_t" or "int64_t"
				or "uint8_t" or "uint16_t" or "uint32_t" or "uint64_t" => FieldKind.Int,
			"float" => FieldKind.Float,
			"string" => FieldKind.String,
			"double" => FieldKind.Double,
			"UID" => FieldKind.Uid,
			"vec2" => FieldKind.Vec2,
			"vec3" => FieldKind.Vec3,
			"vec4" => FieldKind.Vec4,
			"quat" or "quaternion" => FieldKind.Quaternion,
			// unknown types compile but the inspector widget will be wrong
			_ => FieldKind.Int,
		};
	}
}
